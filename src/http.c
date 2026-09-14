#include "ghost.h"

#include <curl/curl.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct Buf {
    char *data;
    size_t len;
    size_t cap;
};

struct DlCtx {
    FILE *fp;
    int (*progress)(void *ud, int64_t total, int64_t now);
    void *ud;
    volatile int *abort_flag;
    int64_t total;
    int64_t now;
};

static pthread_mutex_t g_curl_once = PTHREAD_MUTEX_INITIALIZER;
static int g_curl_inited;

int http_global_init(void) {
    pthread_mutex_lock(&g_curl_once);
    if (!g_curl_inited) {
        if (curl_global_init(CURL_GLOBAL_DEFAULT) != 0) {
            pthread_mutex_unlock(&g_curl_once);
            return -1;
        }
        g_curl_inited = 1;
    }
    pthread_mutex_unlock(&g_curl_once);
    return 0;
}

void http_global_cleanup(void) {
    pthread_mutex_lock(&g_curl_once);
    if (g_curl_inited) {
        curl_global_cleanup();
        g_curl_inited = 0;
    }
    pthread_mutex_unlock(&g_curl_once);
}

static size_t write_buf(char *ptr, size_t size, size_t nmemb, void *userdata) {
    struct Buf *b = userdata;
    size_t n = size * nmemb;
    if (b->len + n + 1 > b->cap) {
        size_t cap = b->cap ? b->cap * 2 : 4096;
        while (cap < b->len + n + 1)
            cap *= 2;
        b->data = xrealloc(b->data, cap);
        b->cap = cap;
    }
    memcpy(b->data + b->len, ptr, n);
    b->len += n;
    b->data[b->len] = 0;
    return n;
}

static size_t write_file(char *ptr, size_t size, size_t nmemb, void *userdata) {
    struct DlCtx *ctx = userdata;
    if (ctx->abort_flag && *ctx->abort_flag)
        return 0;
    size_t n = size * nmemb;
    size_t w = fwrite(ptr, 1, n, ctx->fp);
    ctx->now += (int64_t)w;
    if (ctx->progress)
        ctx->progress(ctx->ud, ctx->total, ctx->now);
    return w;
}

static int xfer(void *clientp, curl_off_t dltotal, curl_off_t dlnow,
                curl_off_t ultotal, curl_off_t ulnow) {
    (void)ultotal;
    (void)ulnow;
    struct DlCtx *ctx = clientp;
    if (ctx->abort_flag && *ctx->abort_flag)
        return 1;
    if (dltotal > 0)
        ctx->total = (int64_t)dltotal;
    ctx->now = (int64_t)dlnow;
    if (ctx->progress)
        return ctx->progress(ctx->ud, ctx->total, ctx->now);
    return 0;
}

static CURL *make_easy(void) {
    CURL *c = curl_easy_init();
    if (!c)
        return NULL;
    curl_easy_setopt(c, CURLOPT_USERAGENT, GHOST_UA);
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_MAXREDIRS, 8L);
    curl_easy_setopt(c, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 120L);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 20L);
    curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(c, CURLOPT_SSL_VERIFYHOST, 2L);
    return c;
}

int http_get(const char *url, char **body, size_t *len, char **final_url) {
    if (body)
        *body = NULL;
    if (len)
        *len = 0;
    if (final_url)
        *final_url = NULL;
    if (!url)
        return -1;
    if (http_global_init() != 0)
        return -1;

    CURL *c = make_easy();
    if (!c)
        return -1;
    struct Buf b = {0};
    curl_easy_setopt(c, CURLOPT_URL, url);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, write_buf);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &b);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 60L);

    CURLcode rc = curl_easy_perform(c);
    long status = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &status);
    if (final_url) {
        char *eff = NULL;
        curl_easy_getinfo(c, CURLINFO_EFFECTIVE_URL, &eff);
        if (eff)
            *final_url = xstrdup(eff);
    }
    curl_easy_cleanup(c);

    if (rc != CURLE_OK) {
        free(b.data);
        return -1;
    }
    if (status >= 400) {
        free(b.data);
        return -1;
    }
    if (!b.data) {
        b.data = xstrdup("");
        b.len = 0;
    }
    if (body)
        *body = b.data;
    else
        free(b.data);
    if (len)
        *len = b.len;
    return 0;
}

int http_download(const char *url, const char *path,
                  int (*progress)(void *ud, int64_t total, int64_t now),
                  void *ud, int64_t *out_total, volatile int *abort_flag) {
    if (!url || !path)
        return -1;
    if (http_global_init() != 0)
        return -1;

    char *part = xmalloc(strlen(path) + 6);
    sprintf(part, "%s.part", path);

    FILE *fp = fopen(part, "wb");
    if (!fp) {
        free(part);
        return -1;
    }

    CURL *c = make_easy();
    if (!c) {
        fclose(fp);
        free(part);
        return -1;
    }

    struct DlCtx ctx = {
        .fp = fp,
        .progress = progress,
        .ud = ud,
        .abort_flag = abort_flag,
        .total = 0,
        .now = 0,
    };

    curl_easy_setopt(c, CURLOPT_URL, url);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, write_file);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &ctx);
    curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, xfer);
    curl_easy_setopt(c, CURLOPT_XFERINFODATA, &ctx);
    curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(c, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 0L); /* large files */

    CURLcode rc = curl_easy_perform(c);
    long status = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &status);
    curl_off_t cl = 0;
    curl_easy_getinfo(c, CURLINFO_SIZE_DOWNLOAD_T, &cl);
    curl_easy_cleanup(c);
    fclose(fp);

    if (abort_flag && *abort_flag) {
        unlink(part);
        free(part);
        return -1;
    }
    if (rc != CURLE_OK || status >= 400) {
        unlink(part);
        free(part);
        return -1;
    }
    if (rename(part, path) != 0) {
        unlink(part);
        free(part);
        return -1;
    }
    free(part);
    if (out_total)
        *out_total = ctx.total > 0 ? ctx.total : ctx.now;
    return 0;
}
