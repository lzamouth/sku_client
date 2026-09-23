/* Common definitions for the SkunkCrafts Updater core. */
#ifndef SKU_H
#define SKU_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define SKU_PATH_MAX 2048    /* UTF-8 bytes, including the terminator */
#define SKU_NAME_MAX 256
#define SKU_URL_MAX  1024

/* Error codes returned by core functions (0 = success). */
enum sku_err {
	SKU_OK = 0,
	SKU_ERR_IO = -1,        /* file system error, see errno / log */
	SKU_ERR_NOTFOUND = -2,
	SKU_ERR_FORMAT = -3,    /* malformed file or protocol data */
	SKU_ERR_NET = -4,       /* network / HTTP error */
	SKU_ERR_UNSAFE = -5,    /* rejected path */
	SKU_ERR_CANCELLED = -6,
	SKU_ERR_NOMEM = -7,
	SKU_ERR_ARG = -8,
};

const char *sku_err_str(int err);

/* util.c */
void   *sku_xmalloc(size_t n);
void   *sku_xcalloc(size_t n, size_t sz);
void   *sku_xrealloc(void *p, size_t n);
char   *sku_xstrdup(const char *s);
size_t  sku_strlcpy(char *dst, const char *src, size_t dstsize);
size_t  sku_strlcat(char *dst, const char *src, size_t dstsize);
int     sku_snprintf(char *dst, size_t dstsize, const char *fmt, ...);
bool    sku_str_ieq(const char *a, const char *b);          /* case-insensitive equal */
bool    sku_str_istarts(const char *s, const char *prefix); /* case-insensitive prefix */
bool    sku_str_ends(const char *s, const char *suffix);
char   *sku_str_trim(char *s);                              /* in place, both ends */
void    sku_str_chomp(char *s);                             /* strip trailing \r \n */
bool    sku_str_to_bool(const char *s);                     /* "true" only, like 3.x */
const char *sku_bool_to_str(bool b);
void    sku_nice_size(char *out, size_t n, int64_t bytes);

/* Growable array of strings (NULL-terminated entries owned by the list). */
typedef struct {
	char **v;
	int n, cap;
} sku_strlist;
void  sku_strlist_push(sku_strlist *l, const char *s);
void  sku_strlist_free(sku_strlist *l);
bool  sku_strlist_has(const sku_strlist *l, const char *s);

/* Split text into lines (in place): '\n' terminated, '\r' stripped. */
int   sku_split_lines(char *text, sku_strlist *out);

#endif
