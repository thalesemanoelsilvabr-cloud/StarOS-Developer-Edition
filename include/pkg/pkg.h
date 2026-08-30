/* ============================================================
 *  StarOS — StarPKG  (include/pkg/pkg.h)
 *  Gerenciador de pacotes estilo APT para StarOS
 *  Formato de pacote: .star (equivalente ao .deb)
 * ============================================================ */
#ifndef STAROS_PKG_H
#define STAROS_PKG_H

#include <kernel/types.h>

/* ── Constantes ─────────────────────────────────────────────── */
#define PKG_NAME_MAX    64
#define PKG_VER_MAX     32
#define PKG_DESC_MAX    256
#define PKG_DEPS_MAX    16
#define PKG_FILES_MAX   256
#define PKG_PATH_MAX    256
#define PKG_REPO_MAX    8
#define PKG_CACHE_MAX   512      /* pacotes no cache de lista   */
#define PKG_DB_PATH     "/etc/pkg/installed.db"
#define PKG_SOURCES     "/etc/pkg/sources.list"
#define PKG_CACHE_DIR   "/var/cache/pkg/"
#define PKG_LISTS_DIR   "/var/lib/pkg/lists/"

/* ── Estruturas ─────────────────────────────────────────────── */
typedef struct {
    char name[PKG_NAME_MAX];
    char version[PKG_VER_MAX];
    char description[PKG_DESC_MAX];
    char depends[PKG_DEPS_MAX][PKG_NAME_MAX];
    int  dep_count;
    u32  installed_size;   /* KB */
    u32  download_size;    /* KB */
    char url[512];         /* URL de download                  */
    char checksum[65];     /* SHA-256 hex                      */
    int  installed;        /* 1 = instalado                    */
    char install_date[32];
} pkg_info_t;

typedef struct {
    char url[512];
    char dist[64];
    char comp[64];
    int  enabled;
} repo_t;

/* ── API ─────────────────────────────────────────────────────── */
/* Inicializa o gerenciador (carrega DB e repositórios) */
void pkg_init(void);

/* Atualiza listas dos repositórios (apt update) */
int  pkg_update(void);

/* Busca pacote por nome. Preenche *info. Retorna 0 se encontrado */
int  pkg_search(const char *name, pkg_info_t *info);

/* Lista todos os pacotes disponíveis (preenchidos em out[max]) */
int  pkg_list(pkg_info_t *out, int max);

/* Lista apenas os instalados */
int  pkg_list_installed(pkg_info_t *out, int max);

/* Instala um pacote (resolve dependências) */
int  pkg_install(const char *name);

/* Remove um pacote */
int  pkg_remove(const char *name);

/* Atualiza todos os pacotes instalados */
int  pkg_upgrade(void);

/* Instala um arquivo .star local */
int  pkg_install_file(const char *path);

/* Instala um arquivo .deb local (conversão automática) */
int  pkg_install_deb(const char *path);

/* Consulta informações de um pacote instalado */
int  pkg_info(const char *name, pkg_info_t *out);

/* Repositórios */
int  pkg_repo_add(const char *url, const char *dist, const char *comp);
int  pkg_repo_list(repo_t *out, int max);

/* Callback de progresso: pkg_set_progress_cb(cb) */
typedef void (*pkg_progress_cb_t)(const char *pkg, int percent);
void pkg_set_progress_cb(pkg_progress_cb_t cb);

#endif /* STAROS_PKG_H */
