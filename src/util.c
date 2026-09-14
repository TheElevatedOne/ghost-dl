#include "ghost.h"

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/ioctl.h>
#endif

static int g_quiet;
static int g_verbose;
static int g_color = -1;

void ghost_log_init(int quiet, int verbose) {
    g_quiet = quiet;
    g_verbose = verbose;
    g_color = -1;
}

int ghost_log_quiet(void) { return g_quiet; }
int ghost_log_verbose(void) { return g_verbose; }

int ghost_is_tty(FILE *fp) {
    return isatty(fileno(fp));
}

int ghost_color_enabled(void) {
    if (g_color >= 0)
        return g_color;
    if (getenv("NO_COLOR"))
        g_color = 0;
    else
        g_color = ghost_is_tty(stderr) || ghost_is_tty(stdout);
    return g_color;
}

void *xmalloc(size_t n) {
    void *p = malloc(n ? n : 1);
    if (!p) {
        fprintf(stderr, "ghost-dl: out of memory\n");
        abort();
    }
    return p;
}

void *xcalloc(size_t n, size_t sz) {
    void *p = calloc(n ? n : 1, sz ? sz : 1);
    if (!p) {
        fprintf(stderr, "ghost-dl: out of memory\n");
        abort();
    }
    return p;
}

void *xrealloc(void *p, size_t n) {
    void *q = realloc(p, n ? n : 1);
    if (!q) {
        fprintf(stderr, "ghost-dl: out of memory\n");
        abort();
    }
    return q;
}

char *xstrdup(const char *s) {
    if (!s)
        return NULL;
    size_t n = strlen(s);
    char *d = xmalloc(n + 1);
    memcpy(d, s, n + 1);
    return d;
}

char *xstrndup(const char *s, size_t n) {
    if (!s)
        return NULL;
    size_t m = 0;
    while (m < n && s[m])
        m++;
    char *d = xmalloc(m + 1);
    memcpy(d, s, m);
    d[m] = 0;
    return d;
}

char *str_trim(char *s) {
    if (!s)
        return s;
    while (*s && isspace((unsigned char)*s))
        s++;
    if (!*s)
        return s;
    char *e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1]))
        e--;
    *e = 0;
    return s;
}

char *str_lower_dup(const char *s) {
    if (!s)
        return NULL;
    char *d = xstrdup(s);
    for (char *p = d; *p; p++)
        *p = (char)tolower((unsigned char)*p);
    return d;
}

int str_eq_ci(const char *a, const char *b) {
    if (!a || !b)
        return a == b;
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b))
            return 0;
        a++;
        b++;
    }
    return *a == *b;
}

int str_ends_ci(const char *s, const char *suf) {
    if (!s || !suf)
        return 0;
    size_t n = strlen(s), m = strlen(suf);
    if (m > n)
        return 0;
    return str_eq_ci(s + n - m, suf);
}

int cpu_threads(void) {
#ifdef _WIN32
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    int n = (int)si.dwNumberOfProcessors;
#else
    long n = sysconf(_SC_NPROCESSORS_ONLN);
#endif
    if (n < 1)
        n = 1;
    return (int)n;
}

int default_thread_count(void) {
    int n = cpu_threads() / 2;
    if (n < 1)
        n = 1;
    if (n > 8)
        n = 8;
    return n;
}

int path_exists(const char *path) {
    struct stat st;
    return path && stat(path, &st) == 0;
}

int path_is_dir(const char *path) {
    struct stat st;
    return path && stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

int path_is_file(const char *path) {
    struct stat st;
    return path && stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

int mkdir_p(const char *path) {
    if (!path || !*path)
        return -1;
    if (path_is_dir(path))
        return 0;
    char *tmp = xstrdup(path);
    size_t n = strlen(tmp);
    while (n > 1 && tmp[n - 1] == '/')
        tmp[--n] = 0;
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            if (!path_is_dir(tmp) && mkdir(tmp, 0755) != 0 && errno != EEXIST) {
                free(tmp);
                return -1;
            }
            *p = '/';
        }
    }
    if (!path_is_dir(tmp) && mkdir(tmp, 0755) != 0 && errno != EEXIST) {
        free(tmp);
        return -1;
    }
    free(tmp);
    return 0;
}

char *path_join(const char *a, const char *b) {
    if (!a || !*a)
        return xstrdup(b ? b : "");
    if (!b || !*b)
        return xstrdup(a);
    size_t na = strlen(a), nb = strlen(b);
    int slash = a[na - 1] == '/';
    char *d = xmalloc(na + nb + 2);
    memcpy(d, a, na);
    if (!slash) {
        d[na] = '/';
        memcpy(d + na + 1, b, nb + 1);
    } else {
        memcpy(d + na, b, nb + 1);
    }
    return d;
}

char *filename_sanitize(const char *name) {
    if (!name)
        return xstrdup("untitled");
    size_t n = strlen(name);
    char *d = xmalloc(n + 2);
    size_t j = 0;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)name[i];
        if (c < 32 || c == 127 || c == '/' || c == '\\' || c == ':' ||
            c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') {
            d[j++] = '_';
        } else {
            d[j++] = (char)c;
        }
    }
    while (j > 0 && (d[j - 1] == ' ' || d[j - 1] == '.'))
        j--;
    d[j] = 0;
    if (!d[0] || strcmp(d, ".") == 0 || strcmp(d, "..") == 0)
        strcpy(d, "untitled");
    return d;
}

char *url_decode(const char *s) {
    if (!s)
        return NULL;
    size_t n = strlen(s);
    char *d = xmalloc(n + 1);
    size_t j = 0;
    for (size_t i = 0; i < n; i++) {
        if (s[i] == '%' && i + 2 < n && isxdigit((unsigned char)s[i + 1]) &&
            isxdigit((unsigned char)s[i + 2])) {
            char hex[3] = {s[i + 1], s[i + 2], 0};
            d[j++] = (char)strtol(hex, NULL, 16);
            i += 2;
        } else if (s[i] == '+') {
            d[j++] = ' ';
        } else {
            d[j++] = s[i];
        }
    }
    d[j] = 0;
    return d;
}

char *url_encode(const char *s) {
    if (!s)
        return NULL;
    size_t n = strlen(s);
    char *d = xmalloc(n * 3 + 1);
    size_t j = 0;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~' || c == ' ') {
            d[j++] = (c == ' ') ? '+' : (char)c;
        } else {
            static const char hex[] = "0123456789ABCDEF";
            d[j++] = '%';
            d[j++] = hex[c >> 4];
            d[j++] = hex[c & 15];
        }
    }
    d[j] = 0;
    return d;
}

char *url_join(const char *base, const char *href) {
    if (!href)
        return xstrdup(base);
    if (strncmp(href, "http://", 7) == 0 || strncmp(href, "https://", 8) == 0)
        return xstrdup(href);
    if (href[0] == '/' && href[1] == '/') {
        char *d = xmalloc(strlen(href) + 8);
        sprintf(d, "https:%s", href);
        return d;
    }
    if (href[0] == '/') {
        /* scheme + host of base */
        const char *p = strstr(base ? base : GHOST_BASE_URL, "://");
        p = p ? p + 3 : (base ? base : GHOST_BASE_URL);
        const char *slash = strchr(p, '/');
        size_t hostlen = slash ? (size_t)(slash - (base ? base : GHOST_BASE_URL))
                               : strlen(base ? base : GHOST_BASE_URL);
        const char *b = base ? base : GHOST_BASE_URL;
        char *d = xmalloc(hostlen + strlen(href) + 1);
        memcpy(d, b, hostlen);
        strcpy(d + hostlen, href);
        return d;
    }
    return path_join(base ? base : GHOST_BASE_URL, href);
}

char *html_unescape(const char *s) {
    if (!s)
        return NULL;
    size_t n = strlen(s);
    char *d = xmalloc(n + 1);
    size_t j = 0;
    for (size_t i = 0; i < n; i++) {
        if (s[i] != '&') {
            d[j++] = s[i];
            continue;
        }
        if (s[i + 1] == '#') {
            int base = 10;
            size_t k = i + 2;
            if (s[k] == 'x' || s[k] == 'X') {
                base = 16;
                k++;
            }
            char *end = NULL;
            long cp = strtol(s + k, &end, base);
            if (end && *end == ';' && cp > 0 && cp < 0x110000) {
                /* UTF-8 encode */
                unsigned long u = (unsigned long)cp;
                if (u < 0x80)
                    d[j++] = (char)u;
                else if (u < 0x800) {
                    d[j++] = (char)(0xC0 | (u >> 6));
                    d[j++] = (char)(0x80 | (u & 0x3F));
                } else if (u < 0x10000) {
                    d[j++] = (char)(0xE0 | (u >> 12));
                    d[j++] = (char)(0x80 | ((u >> 6) & 0x3F));
                    d[j++] = (char)(0x80 | (u & 0x3F));
                } else {
                    d[j++] = (char)(0xF0 | (u >> 18));
                    d[j++] = (char)(0x80 | ((u >> 12) & 0x3F));
                    d[j++] = (char)(0x80 | ((u >> 6) & 0x3F));
                    d[j++] = (char)(0x80 | (u & 0x3F));
                }
                i = (size_t)(end - s);
                continue;
            }
        }
        static const struct {
            const char *ent;
            const char *rep;
        } ents[] = {
            {"&amp;", "&"},   {"&lt;", "<"},   {"&gt;", ">"},
            {"&quot;", "\""}, {"&apos;", "'"}, {"&#39;", "'"},
            {"&nbsp;", " "},  {"&ndash;", "-"}, {"&mdash;", "-"},
            {"&eacute;", "é"}, {"&Eacute;", "É"}, {"&egrave;", "è"},
            {"&agrave;", "à"}, {"&ouml;", "ö"}, {"&uuml;", "ü"},
            {"&auml;", "ä"},   {"&copy;", "(c)"}, {NULL, NULL},
        };
        int hit = 0;
        for (int e = 0; ents[e].ent; e++) {
            size_t el = strlen(ents[e].ent);
            if (strncmp(s + i, ents[e].ent, el) == 0) {
                size_t rl = strlen(ents[e].rep);
                memcpy(d + j, ents[e].rep, rl);
                j += rl;
                i += el - 1;
                hit = 1;
                break;
            }
        }
        if (!hit)
            d[j++] = s[i];
    }
    d[j] = 0;
    return d;
}

char *html_text(const char *s, size_t n) {
    if (!s)
        return xstrdup("");
    if (n == (size_t)-1)
        n = strlen(s);
    char *tmp = xmalloc(n + 1);
    size_t j = 0;
    int in_tag = 0;
    for (size_t i = 0; i < n; i++) {
        if (s[i] == '<') {
            in_tag = 1;
            if (j > 0 && tmp[j - 1] != ' ')
                tmp[j++] = ' ';
            continue;
        }
        if (s[i] == '>') {
            in_tag = 0;
            continue;
        }
        if (!in_tag)
            tmp[j++] = s[i];
    }
    tmp[j] = 0;
    char *un = html_unescape(tmp);
    free(tmp);
    char *t = str_trim(un);
    if (t != un)
        memmove(un, t, strlen(t) + 1);
    /* collapse whitespace */
    j = 0;
    int sp = 0;
    for (char *p = un; *p; p++) {
        if (isspace((unsigned char)*p)) {
            if (!sp) {
                un[j++] = ' ';
                sp = 1;
            }
        } else {
            un[j++] = *p;
            sp = 0;
        }
    }
    un[j] = 0;
    return un;
}

const char *html_find(const char *s, const char *needle) {
    if (!s || !needle)
        return NULL;
    return strstr(s, needle);
}

char *html_extract(const char *s, const char *start, const char *end) {
    if (!s || !start)
        return NULL;
    const char *a = strstr(s, start);
    if (!a)
        return NULL;
    a += strlen(start);
    if (!end)
        return xstrdup(a);
    const char *b = strstr(a, end);
    if (!b)
        return xstrdup(a);
    return xstrndup(a, (size_t)(b - a));
}

int looks_like_url(const char *s) {
    return s && (strncmp(s, "http://", 7) == 0 || strncmp(s, "https://", 8) == 0);
}

int looks_like_khinsider(const char *s) {
    return s && strstr(s, "downloads.khinsider.com") != NULL;
}

char *slug_from_input(const char *in) {
    if (!in || !*in)
        return NULL;
    char *s = xstrdup(in);
    str_trim(s);
    /* strip trailing slash */
    size_t n = strlen(s);
    while (n > 0 && s[n - 1] == '/')
        s[--n] = 0;
    const char *key = "/game-soundtracks/album/";
    char *p = strstr(s, key);
    if (p) {
        p += strlen(key);
        char *q = strchr(p, '/');
        char *out = q ? xstrndup(p, (size_t)(q - p)) : xstrdup(p);
        /* strip query */
        char *h = strpbrk(out, "?#");
        if (h)
            *h = 0;
        free(s);
        return out;
    }
    if (looks_like_url(s)) {
        free(s);
        return NULL;
    }
    /* treat as slug */
    char *h = strpbrk(s, "?#");
    if (h)
        *h = 0;
    return s;
}

char *album_folder_name(const struct Album *al) {
    char buf[1024];
    const char *year = (al->year && al->year[0]) ? al->year : "0000";
    const char *title = al->title ? al->title : "Unknown Album";
    const char *type = (al->type && al->type[0]) ? al->type : "Soundtrack";
    snprintf(buf, sizeof(buf), "%s - %s (%s)", year, title, type);
    return filename_sanitize(buf);
}

char *track_filename(const struct Track *tr, int multi_disc, int num_width) {
    char prefix[64];
    prefix[0] = 0;
    if (multi_disc && tr->disc > 0)
        snprintf(prefix, sizeof(prefix), "CD%d ", tr->disc);
    char *safe = filename_sanitize(tr->title ? tr->title : "track");
    const char *ext = tr->ext && tr->ext[0] ? tr->ext : "mp3";
    char *out = xmalloc(strlen(prefix) + strlen(safe) + 32);
    if (tr->number > 0)
        sprintf(out, "%s%0*d. %s.%s", prefix, num_width, tr->number, safe, ext);
    else
        sprintf(out, "%s%s.%s", prefix, safe, ext);
    free(safe);
    return out;
}

static const char *FORMAT_RANK[] = {
    "flac", "wav", "aiff", "alac", "ape", "tak", "tta",
    "ogg", "opus", "aac", "m4a", "wma", "mp3", NULL
};

int is_audio_format(const char *fmt) {
    if (!fmt || !*fmt)
        return 0;
    for (int i = 0; FORMAT_RANK[i]; i++) {
        if (str_eq_ci(fmt, FORMAT_RANK[i]))
            return 1;
    }
    /* short unknown tokens that look like extensions */
    size_t n = strlen(fmt);
    if (n >= 2 && n <= 5) {
        int ok = 1;
        for (size_t i = 0; i < n; i++) {
            if (!isalnum((unsigned char)fmt[i]))
                ok = 0;
        }
        return ok;
    }
    return 0;
}

int format_rank(const char *fmt) {
    if (!fmt)
        return 1000;
    for (int i = 0; FORMAT_RANK[i]; i++) {
        if (str_eq_ci(fmt, FORMAT_RANK[i]))
            return i;
    }
    return 500;
}

const char *pick_format(char **available, int n, const char *preference, int use_default) {
    if (!available || n <= 0)
        return "mp3";
    if (preference && *preference) {
        char *pref = xstrdup(preference);
        char *save = NULL;
        for (char *tok = strtok_r(pref, ", ", &save); tok; tok = strtok_r(NULL, ", ", &save)) {
            if (*tok == '.')
                tok++;
            for (int i = 0; i < n; i++) {
                if (str_eq_ci(available[i], tok)) {
                    const char *hit = available[i];
                    free(pref);
                    return hit;
                }
            }
        }
        free(pref);
        /* requested format missing: fall through */
    }
    if (use_default || !preference) {
        int best = 0;
        int best_rank = format_rank(available[0]);
        for (int i = 1; i < n; i++) {
            int r = format_rank(available[i]);
            if (r < best_rank) {
                best_rank = r;
                best = i;
            }
        }
        return available[best];
    }
    return available[n - 1];
}

char *read_file_text(const char *path, size_t *len) {
    FILE *fp = fopen(path, "rb");
    if (!fp)
        return NULL;
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return NULL;
    }
    long sz = ftell(fp);
    if (sz < 0) {
        fclose(fp);
        return NULL;
    }
    rewind(fp);
    char *buf = xmalloc((size_t)sz + 1);
    size_t n = fread(buf, 1, (size_t)sz, fp);
    fclose(fp);
    buf[n] = 0;
    if (len)
        *len = n;
    return buf;
}

int64_t monotonic_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

void human_bytes(char *buf, size_t buflen, int64_t n) {
    if (n < 0)
        n = 0;
    const double v = (double)n;
    if (n < 1024)
        snprintf(buf, buflen, "%ld B", (long)n);
    else if (n < 1024 * 1024)
        snprintf(buf, buflen, "%.1f KB", v / 1024.0);
    else if (n < (int64_t)1024 * 1024 * 1024)
        snprintf(buf, buflen, "%.2f MB", v / (1024.0 * 1024.0));
    else
        snprintf(buf, buflen, "%.2f GB", v / (1024.0 * 1024.0 * 1024.0));
}

void human_rate(char *buf, size_t buflen, double bps) {
    if (bps < 0)
        bps = 0;
    human_bytes(buf, buflen, (int64_t)bps);
    size_t n = strlen(buf);
    if (n + 3 < buflen) {
        memcpy(buf + n, "/s", 3);
    }
}

void human_duration(char *buf, size_t buflen, double seconds) {
    if (seconds < 0 || seconds > 1e9) {
        snprintf(buf, buflen, "--:--");
        return;
    }
    int s = (int)(seconds + 0.5);
    int h = s / 3600;
    int m = (s % 3600) / 60;
    int sec = s % 60;
    if (h > 0)
        snprintf(buf, buflen, "%d:%02d:%02d", h, m, sec);
    else
        snprintf(buf, buflen, "%d:%02d", m, sec);
}

void album_free(struct Album *al) {
    if (!al)
        return;
    free(al->id);
    free(al->url);
    free(al->title);
    free(al->year);
    free(al->type);
    free(al->platforms);
    free(al->publisher);
    free(al->filesize);
    free(al->date_added);
    free(al->description);
    for (int i = 0; i < al->nformats; i++)
        free(al->formats[i]);
    free(al->formats);
    for (int i = 0; i < al->ntracks; i++) {
        free(al->tracks[i].title);
        free(al->tracks[i].page_url);
        free(al->tracks[i].download_url);
        free(al->tracks[i].ext);
        free(al->tracks[i].duration);
        free(al->tracks[i].size_str);
    }
    free(al->tracks);
    for (int i = 0; i < al->ncovers; i++) {
        free(al->covers[i].url);
        free(al->covers[i].thumb_url);
    }
    free(al->covers);
    memset(al, 0, sizeof(*al));
}

void search_free(struct SearchResults *sr) {
    if (!sr)
        return;
    for (int i = 0; i < sr->count; i++) {
        free(sr->items[i].id);
        free(sr->items[i].title);
        free(sr->items[i].platforms);
        free(sr->items[i].type);
        free(sr->items[i].year);
    }
    free(sr->items);
    memset(sr, 0, sizeof(*sr));
}

void options_free(struct Options *opt) {
    if (!opt)
        return;
    for (int i = 0; i < opt->ninputs; i++)
        free(opt->inputs[i]);
    free(opt->inputs);
    free(opt->output);
    free(opt->format);
    free(opt->search);
    memset(opt, 0, sizeof(*opt));
}

static void vlog(FILE *fp, const char *color, const char *tag, const char *fmt, va_list ap) {
    if (ghost_color_enabled() && color)
        fprintf(fp, "%s%s\033[0m ", color, tag);
    else
        fprintf(fp, "%s ", tag);
    vfprintf(fp, fmt, ap);
    fputc('\n', fp);
}

void log_info(const char *fmt, ...) {
    if (g_quiet)
        return;
    va_list ap;
    va_start(ap, fmt);
    vlog(stderr, "\033[1;33m", "[LOG]", fmt, ap);
    va_end(ap);
}

void log_error(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vlog(stderr, "\033[31m", "[ERROR]", fmt, ap);
    va_end(ap);
}

void log_verbose(const char *fmt, ...) {
    if (g_quiet || !g_verbose)
        return;
    va_list ap;
    va_start(ap, fmt);
    vlog(stderr, "\033[1;33m", "[LOG]", fmt, ap);
    va_end(ap);
}

void log_input(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vlog(stderr, "\033[1;34m", "[INPUT]", fmt, ap);
    va_end(ap);
}

void log_final(const char *fmt, ...) {
    if (g_quiet)
        return;
    va_list ap;
    va_start(ap, fmt);
    vlog(stderr, "\033[38;2;0;242;255m", "[FINAL]", fmt, ap);
    va_end(ap);
}

void log_dl(const char *fmt, ...) {
    if (g_quiet)
        return;
    va_list ap;
    va_start(ap, fmt);
    vlog(stderr, "\033[38;2;255;0;255m", "[DL]", fmt, ap);
    va_end(ap);
}
