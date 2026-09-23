#include "http.h"
#include "platform.h"
#include "paths.h"
#include "log.h"
#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static char  *g_cacert;
static size_t g_cacert_len;
static char   g_user_agent[128] = "SkunkCraftsUpdater/" SKU_VERSION;

struct sku_http {
	CURL *curl;
	char  errbuf[CURL_ERROR_SIZE];
};

/* per-request state shared by the callbacks */
struct xfer {
	sku_http_result *r;
	/* memory target */
	char   *buf;
	size_t  len, cap;
	/* file target */
	FILE   *file;
	/* progress / cancel */
	sku_progress_cb cb;
	void   *user;
	atomic_int *cancel;
	bool    cancelled;
};

int sku_http_global_init(const char *cacert_pem, size_t cacert_len, const char *user_agent)
{
	if (curl_global_init(CURL_GLOBAL_DEFAULT) != 0) return SKU_ERR_NET;
	if (cacert_pem && cacert_len) {
		/* Schannel uses the Windows store and rejects custom bundles: never hand it one */
		if (!strstr(curl_version_info(CURLVERSION_NOW)->ssl_version, "Schannel")) {
			g_cacert = sku_xmalloc(cacert_len);
			memcpy(g_cacert, cacert_pem, cacert_len);
			g_cacert_len = cacert_len;
		}
	}
	if (user_agent) sku_strlcpy(g_user_agent, user_agent, sizeof g_user_agent);
	return SKU_OK;
}

extern const unsigned char sku_cacert_pem[];
extern const size_t sku_cacert_pem_len;

int sku_http_global_init_default(const char *user_agent)
{
	static const char *system_bundles[] = {
		"/etc/ssl/certs/ca-certificates.crt",   /* Debian, Ubuntu, Arch */
		"/etc/pki/tls/certs/ca-bundle.crt",     /* Fedora, RHEL */
		"/etc/ssl/ca-bundle.pem",               /* openSUSE */
		"/etc/ssl/cert.pem",                    /* macOS, Alpine, FreeBSD */
	};
	if (strcmp(sku_os_name(), "windows") == 0) return sku_http_global_init(NULL, 0, user_agent);   /* Schannel: system store */
	for (size_t i = 0; i < sizeof system_bundles / sizeof *system_bundles; i++) {
		char *pem; size_t len;
		if (sku_read_file(system_bundles[i], &pem, &len) == SKU_OK && len > 1000) {
			int rc = sku_http_global_init(pem, len, user_agent);
			sku_info("TLS: using system CA bundle %s", system_bundles[i]);
			free(pem);
			return rc;
		}
	}
	sku_info("TLS: using the embedded CA bundle (%lu bytes)", (unsigned long)sku_cacert_pem_len);
	return sku_http_global_init((const char *)sku_cacert_pem, sku_cacert_pem_len, user_agent);
}

void sku_http_global_cleanup(void)
{
	free(g_cacert);
	g_cacert = NULL; g_cacert_len = 0;
	curl_global_cleanup();
}

const char *sku_http_version(void)
{
	return curl_version();
}

sku_http *sku_http_new(void)
{
	sku_http *h = sku_xcalloc(1, sizeof *h);
	h->curl = curl_easy_init();
	if (!h->curl) { free(h); return NULL; }
	return h;
}

void sku_http_free(sku_http *h)
{
	if (!h) return;
	curl_easy_cleanup(h->curl);
	free(h);
}

static size_t on_header(char *data, size_t sz, size_t n, void *user)
{
	struct xfer *x = user;
	size_t len = sz * n;
	if (len > 5 && strncasecmp(data, "ETag:", 5) == 0) {
		char tmp[128];
		size_t k = len - 5 < sizeof tmp - 1 ? len - 5 : sizeof tmp - 1;
		memcpy(tmp, data + 5, k); tmp[k] = 0;
		sku_strlcpy(x->r->etag, sku_str_trim(tmp), sizeof x->r->etag);
	} else if (len > 14 && strncasecmp(data, "Last-Modified:", 14) == 0) {
		char tmp[64];
		size_t k = len - 14 < sizeof tmp - 1 ? len - 14 : sizeof tmp - 1;
		memcpy(tmp, data + 14, k); tmp[k] = 0;
		sku_strlcpy(x->r->last_modified, sku_str_trim(tmp), sizeof x->r->last_modified);
	}
	return len;
}

static size_t on_body(char *data, size_t sz, size_t n, void *user)
{
	struct xfer *x = user;
	size_t len = sz * n;
	if (x->file) {
		if (fwrite(data, 1, len, x->file) != len) return 0;
	} else {
		if (x->len + len + 1 > x->cap) {
			while (x->len + len + 1 > x->cap) x->cap = x->cap ? x->cap * 2 : 65536;
			x->buf = sku_xrealloc(x->buf, x->cap);
		}
		memcpy(x->buf + x->len, data, len);
		x->len += len;
		x->buf[x->len] = 0;
	}
	x->r->bytes += (int64_t)len;
	return len;
}

static int on_progress(void *user, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow)
{
	struct xfer *x = user;
	(void)ultotal; (void)ulnow;
	if (x->cancel && atomic_load(x->cancel)) { x->cancelled = true; return 1; }
	if (x->cb && x->cb(x->user, (int64_t)dlnow, (int64_t)dltotal)) { x->cancelled = true; return 1; }
	return 0;
}

static void setup(sku_http *h, const char *url, struct xfer *x)
{
	CURL *c = h->curl;
	curl_easy_reset(c);
	curl_easy_setopt(c, CURLOPT_URL, url);
	curl_easy_setopt(c, CURLOPT_USERAGENT, g_user_agent);
	curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(c, CURLOPT_MAXREDIRS, 8L);
	curl_easy_setopt(c, CURLOPT_ACCEPT_ENCODING, "");           /* transport compression, decoded by curl */
	curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 20L);
	curl_easy_setopt(c, CURLOPT_LOW_SPEED_LIMIT, 1L);           /* stalled for 60 s = failure, no total timeout */
	curl_easy_setopt(c, CURLOPT_LOW_SPEED_TIME, 60L);
	curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
	curl_easy_setopt(c, CURLOPT_FAILONERROR, 0L);               /* we read the status ourselves */
	curl_easy_setopt(c, CURLOPT_ERRORBUFFER, h->errbuf);
	curl_easy_setopt(c, CURLOPT_HEADERFUNCTION, on_header);
	curl_easy_setopt(c, CURLOPT_HEADERDATA, x);
	curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, on_body);
	curl_easy_setopt(c, CURLOPT_WRITEDATA, x);
	curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, on_progress);
	curl_easy_setopt(c, CURLOPT_XFERINFODATA, x);
	curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
	if (g_cacert) {
		struct curl_blob blob = { g_cacert, g_cacert_len, CURL_BLOB_NOCOPY };
		curl_easy_setopt(c, CURLOPT_CAINFO_BLOB, &blob);
	}
	h->errbuf[0] = 0;
}

/* Runs the transfer, fills r, returns SKU_OK only for the accepted status. */
static int perform(sku_http *h, struct xfer *x, long accepted)
{
	CURL *c = h->curl;
	sku_http_result *r = x->r;
	CURLcode rc = curl_easy_perform(c);
	const char *s = NULL;

	curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &r->status);
	curl_easy_getinfo(c, CURLINFO_CONTENT_TYPE, &s);
	if (s) sku_strlcpy(r->content_type, s, sizeof r->content_type);
	s = NULL;
	curl_easy_getinfo(c, CURLINFO_EFFECTIVE_URL, &s);
	if (s) sku_strlcpy(r->effective_url, s, sizeof r->effective_url);

	if (x->cancelled) { sku_strlcpy(r->error, "cancelled", sizeof r->error); return SKU_ERR_CANCELLED; }
	if (rc != CURLE_OK) {
		sku_snprintf(r->error, sizeof r->error, "%s", h->errbuf[0] ? h->errbuf : curl_easy_strerror(rc));
		return SKU_ERR_NET;
	}
	if (r->status == 304 && accepted == 200) { r->not_modified = true; return SKU_OK; }
	if (r->status != accepted) {
		sku_snprintf(r->error, sizeof r->error, "HTTP %ld", r->status);
		return SKU_ERR_NET;
	}
	return SKU_OK;
}

int sku_http_get_mem(sku_http *h, const char *url, const char *etag_in, const char *since_in, char **out, size_t *len, sku_http_result *r)
{
	struct xfer x = {0};
	struct curl_slist *hdrs = NULL;
	int rc;
	memset(r, 0, sizeof *r);
	x.r = r;
	setup(h, url, &x);
	if (etag_in && *etag_in) {
		char line[192];
		sku_snprintf(line, sizeof line, "If-None-Match: %s", etag_in);
		hdrs = curl_slist_append(hdrs, line);
	}
	if (since_in && *since_in) {
		char line[128];
		sku_snprintf(line, sizeof line, "If-Modified-Since: %s", since_in);
		hdrs = curl_slist_append(hdrs, line);
	}
	if (hdrs) curl_easy_setopt(h->curl, CURLOPT_HTTPHEADER, hdrs);
	rc = perform(h, &x, 200);
	curl_slist_free_all(hdrs);
	*out = NULL;
	if (len) *len = 0;
	if (rc != SKU_OK || r->not_modified) { free(x.buf); return rc; }
	if (!x.buf) { x.buf = sku_xmalloc(1); x.buf[0] = 0; }
	*out = x.buf;
	if (len) *len = x.len;
	return SKU_OK;
}

int sku_http_get_file(sku_http *h, const char *url, const char *dest, int64_t expected_size,
                      sku_progress_cb cb, void *user, atomic_int *cancel, sku_http_result *r)
{
	struct xfer x = {0};
	char part[SKU_PATH_MAX], dir[SKU_PATH_MAX];
	int rc;
	memset(r, 0, sizeof *r);
	x.r = r; x.cb = cb; x.user = user; x.cancel = cancel;

	if (sku_snprintf(part, sizeof part, "%s.skupart", dest) >= (int)sizeof part) return SKU_ERR_ARG;
	if (strchr(dest, '/')) {
		sku_path_dirname(dir, sizeof dir, dest);
		if (sku_mkdir_p(dir) != SKU_OK) { sku_snprintf(r->error, sizeof r->error, "cannot create %s", dir); return SKU_ERR_IO; }
	}
	x.file = sku_fopen(part, "wb");
	if (!x.file) { sku_snprintf(r->error, sizeof r->error, "cannot create %s", part); return SKU_ERR_IO; }

	setup(h, url, &x);
	rc = perform(h, &x, 200);
	if (fclose(x.file) != 0 && rc == SKU_OK) { sku_strlcpy(r->error, "write error", sizeof r->error); rc = SKU_ERR_IO; }
	if (rc == SKU_OK && expected_size >= 0 && r->bytes != expected_size) {
		sku_snprintf(r->error, sizeof r->error, "size mismatch: got %lld, expected %lld", (long long)r->bytes, (long long)expected_size);
		rc = SKU_ERR_NET;
	}
	if (rc != SKU_OK) { sku_remove_file(part); return rc; }
	if (sku_rename(part, dest) != SKU_OK) {
		sku_snprintf(r->error, sizeof r->error, "cannot rename %s", part);
		sku_remove_file(part);
		return SKU_ERR_IO;
	}
	return SKU_OK;
}

bool sku_http_looks_like_html(const char *data, size_t len, const char *content_type)
{
	size_t i = 0;
	if (content_type && sku_str_istarts(content_type, "text/html")) return true;
	while (i < len && isspace((unsigned char)data[i])) i++;
	if (len - i >= 9 && strncasecmp(data + i, "<!DOCTYPE", 9) == 0) return true;
	if (len - i >= 5 && strncasecmp(data + i, "<html", 5) == 0) return true;
	return false;
}
