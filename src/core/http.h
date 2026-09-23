/* HTTP client on top of libcurl.
 * One sku_http handle per thread (curl easy handles are not shareable);
 * the handle keeps its connections alive between requests. */
#ifndef SKU_HTTP_H
#define SKU_HTTP_H

#include "sku.h"
#include <stdatomic.h>

typedef struct sku_http sku_http;

typedef struct {
	long    status;               /* HTTP status, 0 if the request never got a response */
	int64_t bytes;                /* body bytes received */
	bool    not_modified;         /* 304: the caller's cached copy is still valid */
	char    content_type[96];
	char    etag[128];
	char    error[256];           /* human readable, filled on failure */
	char    effective_url[SKU_URL_MAX];
} sku_http_result;

/* Returns non-zero to cancel the transfer. */
typedef int (*sku_progress_cb)(void *user, int64_t received, int64_t total);

/* cacert may be NULL (Schannel on Windows uses the system store). */
int  sku_http_global_init(const char *cacert_pem, size_t cacert_len, const char *user_agent);
void sku_http_global_cleanup(void);
const char *sku_http_version(void);

sku_http *sku_http_new(void);
void      sku_http_free(sku_http *h);

/* GET into memory. Returns SKU_OK for 2xx (*out malloc'd, NUL-terminated),
 * SKU_OK with r->not_modified for 304 when etag_in matched (*out NULL),
 * SKU_ERR_NET otherwise (details in r). */
int sku_http_get_mem(sku_http *h, const char *url, const char *etag_in, char **out, size_t *len, sku_http_result *r);

/* GET to a file: the body goes to "<dest>.skupart" then is renamed over dest,
 * so dest is never left truncated. Fails unless the status is 200 and, when
 * expected_size >= 0, the size matches. cb and cancel may be NULL. */
int sku_http_get_file(sku_http *h, const char *url, const char *dest, int64_t expected_size,
                      sku_progress_cb cb, void *user, atomic_int *cancel, sku_http_result *r);

/* True when a body that should be a protocol text file looks like an HTML page. */
bool sku_http_looks_like_html(const char *data, size_t len, const char *content_type);

#endif
