#include "ghost.h"

#include <curl/curl.h>
#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define HTTP_TRIES 4

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
    int64_t offset; /* bytes already on disk when resuming */
    int64_t total;
    int64_t now;
};

static pthread_mutex_t g_curl_once = PTHREAD_MUTEX_INITIALIZER;
static int g_curl_inited;
static volatile int *g_abort;
/* One reusable handle per thread keeps connections alive between page fetches. */
static pthread_key_t g_handle_key;

static void handle_dtor(void *p) {
    if (p)
        curl_easy_cleanup(p);
}

int http_global_init(void) {
    pthread_mutex_lock(&g_curl_once);
    if (!g_curl_inited) {
        if (curl_global_init(CURL_GLOBAL_DEFAULT) != 0) {
            pthread_mutex_unlock(&g_curl_once);
            return -1;
        }
        if (pthread_key_create(&g_handle_key, handle_dtor) != 0) {
            curl_global_cleanup();
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
        /* thread destructors do not run for the main thread */
        handle_dtor(pthread_getspecific(g_handle_key));
        pthread_setspecific(g_handle_key, NULL);
        pthread_key_delete(g_handle_key);
        curl_global_cleanup();
        g_curl_inited = 0;
    }
    pthread_mutex_unlock(&g_curl_once);
}

void http_set_abort_flag(volatile int *flag) {
    g_abort = flag;
}

static int aborted(volatile int *flag) {
    return (flag && *flag) || (g_abort && *g_abort);
}

/* Sleep in small steps so Ctrl-C stays responsive during backoff. */
static void backoff(int attempt, volatile int *flag) {
    int ms = 600 << attempt;
    for (int waited = 0; waited < ms && !aborted(flag); waited += 50)
        usleep(50000);
}

static int retryable_status(long status) {
    return status == 403 || status == 408 || status == 425 || status == 429 ||
           status >= 500;
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

static int get_xfer(void *clientp, curl_off_t dltotal, curl_off_t dlnow,
                    curl_off_t ultotal, curl_off_t ulnow) {
    (void)clientp;
    (void)dltotal;
    (void)dlnow;
    (void)ultotal;
    (void)ulnow;
    return aborted(NULL);
}

static size_t write_file(char *ptr, size_t size, size_t nmemb, void *userdata) {
    struct DlCtx *ctx = userdata;
    if (aborted(ctx->abort_flag))
        return 0;
    size_t n = size * nmemb;
    size_t w = fwrite(ptr, 1, n, ctx->fp);
    ctx->now += (int64_t)w;
    if (ctx->progress)
        ctx->progress(ctx->ud, ctx->total, ctx->now);
    return w;
}

static int dl_xfer(void *clientp, curl_off_t dltotal, curl_off_t dlnow,
                   curl_off_t ultotal, curl_off_t ulnow) {
    (void)ultotal;
    (void)ulnow;
    struct DlCtx *ctx = clientp;
    if (aborted(ctx->abort_flag))
        return 1;
    if (dltotal > 0)
        ctx->total = ctx->offset + (int64_t)dltotal;
    ctx->now = ctx->offset + (int64_t)dlnow;
    if (ctx->progress)
        return ctx->progress(ctx->ud, ctx->total, ctx->now);
    return 0;
}

/*
 * KHInsider sits behind Cloudflare, which rejects requests that do not look
 * like a browser. HTTP/2 and curl's default Accept-Encoding list are both
 * fingerprinted, so stay on HTTP/1.1 with plain browser headers.
 */
static void setup_easy(CURL *c, const char *referer, struct curl_slist **hdrs, int page) {
    *hdrs = NULL;
    *hdrs = curl_slist_append(*hdrs, page
        ? "Accept: text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8"
        : "Accept: */*");
    *hdrs = curl_slist_append(*hdrs, "Accept-Language: en-US,en;q=0.9");
    curl_easy_setopt(c, CURLOPT_HTTPHEADER, *hdrs);
    curl_easy_setopt(c, CURLOPT_HTTP_VERSION, (long)CURL_HTTP_VERSION_1_1);
    curl_easy_setopt(c, CURLOPT_USERAGENT, GHOST_UA);
    curl_easy_setopt(c, CURLOPT_REFERER, referer ? referer : GHOST_BASE_URL "/");
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_MAXREDIRS, 8L);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 20L);
    curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(c, CURLOPT_SSL_VERIFYHOST, 2L);
    /* Give up on a connection that stalls for 30 s; the caller retries. */
    curl_easy_setopt(c, CURLOPT_LOW_SPEED_LIMIT, 1L);
    curl_easy_setopt(c, CURLOPT_LOW_SPEED_TIME, 30L);
}

static CURL *make_easy(const char *referer, struct curl_slist **hdrs, int page) {
    CURL *c = curl_easy_init();
    if (c)
        setup_easy(c, referer, hdrs, page);
    return c;
}

static CURL *thread_handle(const char *referer, struct curl_slist **hdrs) {
    CURL *c = pthread_getspecific(g_handle_key);
    if (c) {
        curl_easy_reset(c);
    } else {
        c = curl_easy_init();
        if (!c)
            return NULL;
        pthread_setspecific(g_handle_key, c);
    }
    setup_easy(c, referer, hdrs, 1);
    return c;
}

int http_get(const char *url, const char *referer, char **body, size_t *len,
             char **final_url, long *status_out) {
    if (body)
        *body = NULL;
    if (len)
        *len = 0;
    if (final_url)
        *final_url = NULL;
    if (status_out)
        *status_out = 0;
    if (!url)
        return -1;
    if (http_global_init() != 0)
        return -1;

    for (int attempt = 0; attempt < HTTP_TRIES; attempt++) {
        if (aborted(NULL))
            return -1;
        if (attempt > 0)
            backoff(attempt - 1, NULL);

        struct curl_slist *hdrs = NULL;
        CURL *c = thread_handle(referer, &hdrs);
        if (!c)
            return -1;
        struct Buf b = {0};
        curl_easy_setopt(c, CURLOPT_URL, url);
        curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, write_buf);
        curl_easy_setopt(c, CURLOPT_WRITEDATA, &b);
        curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, get_xfer);
        curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(c, CURLOPT_TIMEOUT, 60L);

        CURLcode rc = curl_easy_perform(c);
        long status = 0;
        curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &status);
        if (status_out)
            *status_out = status;
        char *eff = NULL;
        curl_easy_getinfo(c, CURLINFO_EFFECTIVE_URL, &eff);
        char *eff_copy = eff ? xstrdup(eff) : NULL;
        curl_easy_setopt(c, CURLOPT_HTTPHEADER, NULL);
        curl_slist_free_all(hdrs);

        if (rc != CURLE_OK || status >= 400) {
            free(b.data);
            free(eff_copy);
            if (rc != CURLE_OK)
                log_verbose("GET %s: %s", url, curl_easy_strerror(rc));
            else
                log_verbose("GET %s: HTTP %ld", url, status);
            if (rc == CURLE_ABORTED_BY_CALLBACK)
                return -1;
            if (rc == CURLE_OK && !retryable_status(status))
                return -1;
            continue;
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
        if (final_url)
            *final_url = eff_copy;
        else
            free(eff_copy);
        return 0;
    }
    return -1;
}

static int64_t file_size(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode))
        return -1;
    return (int64_t)st.st_size;
}

/*
 * Download into PATH.part, then rename. An existing .part file is resumed
 * with a Range request, so interrupted downloads continue where they left off.
 */
int http_download(const char *url, const char *path, const char *referer,
                  int (*progress)(void *ud, int64_t total, int64_t now),
                  void *ud, int64_t *out_total, volatile int *abort_flag) {
    if (!url || !path)
        return -1;
    if (http_global_init() != 0)
        return -1;

    char *part = xmalloc(strlen(path) + 6);
    sprintf(part, "%s.part", path);

    int result = -1;
    for (int attempt = 0; attempt < HTTP_TRIES; attempt++) {
        if (aborted(abort_flag))
            break;
        if (attempt > 0)
            backoff(attempt - 1, abort_flag);

        int64_t offset = file_size(part);
        if (offset < 0)
            offset = 0;
        FILE *fp = fopen(part, offset > 0 ? "ab" : "wb");
        if (!fp) {
            log_verbose("Cannot write %s: %s", part, strerror(errno));
            break;
        }

        struct curl_slist *hdrs = NULL;
        CURL *c = make_easy(referer, &hdrs, 0);
        if (!c) {
            fclose(fp);
            break;
        }

        struct DlCtx ctx = {
            .fp = fp,
            .progress = progress,
            .ud = ud,
            .abort_flag = abort_flag,
            .offset = offset,
            .total = 0,
            .now = offset,
        };

        curl_easy_setopt(c, CURLOPT_URL, url);
        curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, write_file);
        curl_easy_setopt(c, CURLOPT_WRITEDATA, &ctx);
        curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, dl_xfer);
        curl_easy_setopt(c, CURLOPT_XFERINFODATA, &ctx);
        curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(c, CURLOPT_FAILONERROR, 1L);
        curl_easy_setopt(c, CURLOPT_TIMEOUT, 0L); /* large files */
        if (offset > 0)
            curl_easy_setopt(c, CURLOPT_RESUME_FROM_LARGE, (curl_off_t)offset);

        CURLcode rc = curl_easy_perform(c);
        long status = 0;
        curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &status);
        curl_easy_cleanup(c);
        curl_slist_free_all(hdrs);
        int close_rc = fclose(fp);

        if (rc == CURLE_ABORTED_BY_CALLBACK || aborted(abort_flag)) {
            /* keep a non-empty .part file for a later resume */
            if (file_size(part) == 0)
                unlink(part);
            break;
        }

        if (offset > 0 && (rc == CURLE_RANGE_ERROR || status == 416)) {
            /* Server will not resume this file: start over. */
            log_verbose("Cannot resume %s, restarting", path);
            unlink(part);
            continue;
        }
        if (rc != CURLE_OK || status >= 400 || close_rc != 0) {
            if (rc != CURLE_OK)
                log_verbose("GET %s: %s", url, curl_easy_strerror(rc));
            else
                log_verbose("GET %s: HTTP %ld", url, status);
            if (rc == CURLE_WRITE_ERROR || close_rc != 0) {
                /* Disk full or similar: retrying will not help. */
                unlink(part);
                break;
            }
            if (rc == CURLE_HTTP_RETURNED_ERROR && !retryable_status(status)) {
                unlink(part);
                break;
            }
            continue;
        }

        if (rename(part, path) != 0) {
            log_verbose("Cannot rename %s: %s", part, strerror(errno));
            unlink(part);
            break;
        }
        if (out_total) {
            int64_t sz = file_size(path);
            *out_total = sz >= 0 ? sz : (ctx.total > 0 ? ctx.total : ctx.now);
        }
        result = 0;
        break;
    }
    free(part);
    return result;
}
