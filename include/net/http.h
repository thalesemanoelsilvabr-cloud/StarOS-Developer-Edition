/* ============================================================
 *  StarOS — HTTP Client  (include/net/http.h)
 *  Requisições HTTP/1.1 GET para o browser
 * ============================================================ */
#ifndef STAROS_HTTP_H
#define STAROS_HTTP_H

#include <kernel/types.h>

#define HTTP_MAX_HEADERS  32
#define HTTP_HDR_NAME_LEN 64
#define HTTP_HDR_VAL_LEN  256
#define HTTP_BODY_MAX     65536  /* 64 KB por página */
#define HTTP_URL_MAX      512

typedef struct {
    int    status;          /* 200, 301, 404 … */
    char   header_names[HTTP_MAX_HEADERS][HTTP_HDR_NAME_LEN];
    char   header_values[HTTP_MAX_HEADERS][HTTP_HDR_VAL_LEN];
    int    header_count;
    u8    *body;            /* kmalloc'd, caller deve kfree */
    u32    body_len;
    char   redirect_url[HTTP_URL_MAX];  /* preenchido em 3xx */
} http_response_t;

/* Realiza GET síncrono. Retorna 0 em sucesso.             */
int  http_get(const char *url, http_response_t *resp);

/* Libera memória do body (chama kfree) */
void http_response_free(http_response_t *resp);

/* Parser HTML mínimo → texto simples + links              */
typedef struct {
    char text[HTTP_BODY_MAX];  /* conteúdo legível          */
    char links[64][HTTP_URL_MAX];
    int  link_count;
    char title[256];
} html_doc_t;

int  html_parse(const u8 *html, u32 len, html_doc_t *out);

#endif /* STAROS_HTTP_H */
