#include "ghost.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

enum DlState { DL_QUEUED, DL_ACTIVE, DL_DONE, DL_FAIL, DL_SKIP };

struct DlItem {
    char *url;
    char *path;
    char *label;
    enum DlState state;
    int64_t total;
    int64_t have;
    char error[96];
};

struct DlPool {
    struct DlItem *items;
    int nitems;
    int threads;
    int next;
    int force;
    volatile int *abort_flag;
    pthread_mutex_t mu;
    int64_t start_ms;
    int64_t last_have;
    int64_t last_ms;
    double speed;
};

static int term_width(void) {
#ifdef TIOCGWINSZ
    struct winsize ws;
    if (ioctl(STDERR_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col >= 40)
        return ws.ws_col;
#endif
    const char *e = getenv("COLUMNS");
    if (e && atoi(e) > 20)
        return atoi(e);
    return 80;
}

static void bar_fill(char *buf, int width, double frac) {
    if (width < 2) {
        buf[0] = 0;
        return;
    }
    if (frac < 0)
        frac = 0;
    if (frac > 1)
        frac = 1;
    int inner = width;
    int filled = (int)(frac * inner + 0.5);
    if (filled > inner)
        filled = inner;
    for (int i = 0; i < inner; i++)
        buf[i] = (i < filled) ? '#' : '-';
    buf[inner] = 0;
}

static const char *state_label(enum DlState s) {
    switch (s) {
    case DL_QUEUED: return "wait";
    case DL_ACTIVE: return "dl  ";
    case DL_DONE:   return "done";
    case DL_FAIL:   return "fail";
    case DL_SKIP:   return "skip";
    }
    return "    ";
}

static void pool_totals(const struct DlPool *p, int *done, int *fail, int *skip,
                        int *active, int64_t *have, int64_t *total) {
    *done = *fail = *skip = *active = 0;
    *have = *total = 0;
    for (int i = 0; i < p->nitems; i++) {
        const struct DlItem *it = &p->items[i];
        *have += it->have;
        *total += it->total > 0 ? it->total : it->have;
        if (it->state == DL_DONE)
            (*done)++;
        else if (it->state == DL_FAIL)
            (*fail)++;
        else if (it->state == DL_SKIP)
            (*skip)++;
        else if (it->state == DL_ACTIVE)
            (*active)++;
    }
}

static int draw_progress(struct DlPool *p, int *last_rows) {
    if (ghost_log_quiet() || !ghost_is_tty(stderr))
        return 0;

    int done, fail, skip, active;
    int64_t have, total;
    pool_totals(p, &done, &fail, &skip, &active, &have, &total);
    int finished = done + fail + skip;
    double frac = (p->nitems > 0) ? (double)finished / (double)p->nitems : 0;
    if (total > 0)
        frac = (double)have / (double)total;

    int64_t now = monotonic_ms();
    if (p->last_ms > 0 && now > p->last_ms) {
        double inst = (double)(have - p->last_have) * 1000.0 / (double)(now - p->last_ms);
        if (inst < 0)
            inst = 0;
        p->speed = p->speed > 0 ? p->speed * 0.7 + inst * 0.3 : inst;
    }
    p->last_have = have;
    p->last_ms = now;

    double elapsed = (now - p->start_ms) / 1000.0;
    double eta = (p->speed > 1 && frac > 0 && frac < 1) ? ((1.0 - frac) * (have / frac - have) / p->speed) : -1;
    if (frac >= 1)
        eta = 0;

    int tw = term_width();
    if (tw < 50)
        tw = 50;
    int color = ghost_color_enabled();

    char *block = xmalloc(8192);
    size_t used = 0;
    int rows = 0;
    #define APP(fmt, ...) do { \
        int _n = snprintf(block + used, 8192 - used, fmt, ##__VA_ARGS__); \
        if (_n > 0) used += (size_t)_n; \
        rows++; \
    } while (0)

    /* clear previous */
    if (*last_rows > 0)
        fprintf(stderr, "\033[%dA\033[J", *last_rows);

    int shown = 0;
    for (int i = 0; i < p->nitems && shown < p->threads; i++) {
        struct DlItem *it = &p->items[i];
        if (it->state != DL_ACTIVE)
            continue;
        double f = (it->total > 0) ? (double)it->have / (double)it->total : 0;
        const char *lab = it->label ? it->label : "";
        int maxn = tw - 48;
        if (maxn < 12)
            maxn = 12;
        char name[256];
        snprintf(name, sizeof(name), "%s", lab);
        if ((int)strlen(name) > maxn) {
            name[maxn - 1] = 0;
            name[maxn - 2] = '.';
            name[maxn - 3] = '.';
            name[maxn - 4] = '.';
        }
        if (it->total <= 0) {
            if (color)
                APP("  \033[38;2;255;0;255m%s\033[0m  %s  starting...\n",
                    state_label(it->state), name);
            else
                APP("  %s  %s  starting...\n", state_label(it->state), name);
        } else {
            char b[41];
            bar_fill(b, 28, f);
            char hs[32], ht[32];
            human_bytes(hs, sizeof(hs), it->have);
            human_bytes(ht, sizeof(ht), it->total);
            if (color)
                APP("  \033[38;2;255;0;255m%s\033[0m  %s  [%s] %5.1f%%  %s / %s\n",
                    state_label(it->state), name, b, f * 100.0, hs, ht);
            else
                APP("  %s  %s  [%s] %5.1f%%  %s / %s\n",
                    state_label(it->state), name, b, f * 100.0, hs, ht);
        }
        shown++;
    }
    if (shown == 0) {
        /* show last completed if nothing active */
        for (int i = p->nitems - 1; i >= 0; i--) {
            if (p->items[i].state == DL_DONE || p->items[i].state == DL_SKIP) {
                const char *lab = p->items[i].label ? p->items[i].label : "";
                APP("  %s  %s\n", state_label(p->items[i].state), lab);
                break;
            }
        }
    }

    char ob[49];
    bar_fill(ob, 40, frac);
    char hh[32], htt[32], hr[32], he[32], het[32];
    human_bytes(hh, sizeof(hh), have);
    human_bytes(htt, sizeof(htt), total);
    human_rate(hr, sizeof(hr), p->speed);
    human_duration(he, sizeof(he), elapsed);
    human_duration(het, sizeof(het), eta);

    if (color)
        APP("\033[38;2;0;242;255m  [%s]\033[0m %5.1f%%\n", ob, frac * 100.0);
    else
        APP("  [%s] %5.1f%%\n", ob, frac * 100.0);
    APP("  %d/%d files  %s / %s  %s  elapsed %s  ETA %s  fail %d  skip %d\n",
        finished, p->nitems, hh, htt, hr, he, het, fail, skip);

    fwrite(block, 1, used, stderr);
    fflush(stderr);
    *last_rows = rows;
    free(block);
    return 0;
#undef APP
}

static int item_progress(void *ud, int64_t total, int64_t now) {
    struct DlItem *it = ud;
    it->total = total;
    it->have = now;
    return 0;
}

static void *dl_worker(void *arg) {
    struct DlPool *p = arg;
    for (;;) {
        if (p->abort_flag && *p->abort_flag)
            return NULL;
        pthread_mutex_lock(&p->mu);
        int i = p->next++;
        pthread_mutex_unlock(&p->mu);
        if (i >= p->nitems)
            return NULL;
        struct DlItem *it = &p->items[i];
        if (path_is_file(it->path) && !p->force) {
            it->state = DL_SKIP;
            FILE *fp = fopen(it->path, "rb");
            if (fp) {
                fseek(fp, 0, SEEK_END);
                it->have = it->total = (int64_t)ftell(fp);
                fclose(fp);
            }
            continue;
        }
        it->state = DL_ACTIVE;
        int rc = -1;
        for (int try = 0; try < 3; try++) {
            if (p->abort_flag && *p->abort_flag)
                break;
            rc = http_download(it->url, it->path, item_progress, it, &it->total, p->abort_flag);
            if (rc == 0)
                break;
        }
        if (rc == 0) {
            it->state = DL_DONE;
            if (it->total > 0)
                it->have = it->total;
        } else {
            it->state = DL_FAIL;
            snprintf(it->error, sizeof(it->error), "download failed");
        }
    }
}

static int run_pool(struct DlPool *p) {
    if (p->nitems <= 0)
        return 0;
    int n = p->threads;
    if (n < 1)
        n = 1;
    if (n > p->nitems)
        n = p->nitems;
    p->threads = n;
    p->start_ms = monotonic_ms();
    p->last_ms = p->start_ms;
    pthread_mutex_init(&p->mu, NULL);

    pthread_t *th = xcalloc((size_t)n, sizeof(pthread_t));
    int started = 0;
    for (int i = 0; i < n; i++) {
        if (pthread_create(&th[i], NULL, dl_worker, p) != 0)
            break;
        started++;
    }

    int last_rows = 0;
    int hide = ghost_is_tty(stderr) && !ghost_log_quiet();
    if (hide)
        fputs("\033[?25l", stderr);

    for (;;) {
        int done, fail, skip, active;
        int64_t have, total;
        pool_totals(p, &done, &fail, &skip, &active, &have, &total);
        if (done + fail + skip >= p->nitems)
            break;
        if (p->abort_flag && *p->abort_flag)
            break;
        draw_progress(p, &last_rows);
        usleep(120000);
    }
    draw_progress(p, &last_rows);
    if (hide)
        fputs("\033[?25h", stderr);
    if (!ghost_log_quiet())
        fputc('\n', stderr);

    for (int i = 0; i < started; i++)
        pthread_join(th[i], NULL);
    free(th);
    pthread_mutex_destroy(&p->mu);

    int fail = 0;
    for (int i = 0; i < p->nitems; i++) {
        if (p->items[i].state == DL_FAIL)
            fail++;
        if (p->items[i].state == DL_FAIL && ghost_log_verbose())
            log_error("Failed: %s", p->items[i].label);
    }
    return fail ? -1 : 0;
}

static void pool_free(struct DlPool *p) {
    for (int i = 0; i < p->nitems; i++) {
        free(p->items[i].url);
        free(p->items[i].path);
        free(p->items[i].label);
    }
    free(p->items);
}

int prompt_format(const struct Album *al, const struct Options *opt, char *out, size_t outsz) {
    const char *picked = pick_format(al->formats, al->nformats, opt->format, opt->use_default);
    if (opt->format && *opt->format) {
        snprintf(out, outsz, "%s", picked);
        return 0;
    }
    if (opt->use_default || !ghost_is_tty(stdin) || ghost_log_quiet()) {
        snprintf(out, outsz, "%s", picked);
        return 0;
    }
    log_input("Enter a number and press Return/Enter");
    for (int i = 0; i < al->nformats; i++) {
        const char *def = (al->formats[i] == picked || str_eq_ci(al->formats[i], picked))
                              ? "(Default)"
                              : "";
        fprintf(stderr, "  %d) %s %s\n", i, al->formats[i], def);
    }
    for (;;) {
        fprintf(stderr, ": ");
        fflush(stderr);
        char line[64];
        if (!fgets(line, sizeof(line), stdin)) {
            snprintf(out, outsz, "%s", picked);
            return 0;
        }
        char *t = str_trim(line);
        if (!*t) {
            snprintf(out, outsz, "%s", picked);
            return 0;
        }
        /* allow typing the format name */
        for (int i = 0; i < al->nformats; i++) {
            if (str_eq_ci(t, al->formats[i])) {
                snprintf(out, outsz, "%s", al->formats[i]);
                return 0;
            }
        }
        char *end = NULL;
        long v = strtol(t, &end, 10);
        if (end != t && v >= 0 && v < al->nformats) {
            snprintf(out, outsz, "%s", al->formats[v]);
            return 0;
        }
        log_error("Not a valid input");
        log_input("Enter a number and press Return/Enter");
        for (int i = 0; i < al->nformats; i++)
            fprintf(stderr, "  %d) %s\n", i, al->formats[i]);
    }
}

void print_album_info(const struct Album *al, FILE *fp) {
    fprintf(fp, "Title:      %s\n", al->title ? al->title : "-");
    fprintf(fp, "ID:         %s\n", al->id ? al->id : "-");
    fprintf(fp, "URL:        %s\n", al->url ? al->url : "-");
    fprintf(fp, "Year:       %s\n", al->year ? al->year : "-");
    fprintf(fp, "Type:       %s\n", al->type ? al->type : "-");
    fprintf(fp, "Platforms:  %s\n", al->platforms ? al->platforms : "-");
    fprintf(fp, "Publisher:  %s\n", al->publisher ? al->publisher : "-");
    fprintf(fp, "Date added: %s\n", al->date_added ? al->date_added : "-");
    fprintf(fp, "Files:      %d\n", al->ntracks);
    fprintf(fp, "Filesize:   %s\n", al->filesize ? al->filesize : "-");
    fprintf(fp, "Covers:     %d\n", al->ncovers);
    fprintf(fp, "Formats:    ");
    for (int i = 0; i < al->nformats; i++)
        fprintf(fp, "%s%s", al->formats[i], (i + 1 < al->nformats) ? ", " : "");
    fprintf(fp, "\n");
    if (al->description && al->description[0])
        fprintf(fp, "Description:\n  %s\n", al->description);
    fprintf(fp, "\nTracks:\n");
    int multi = 0;
    int maxn = 0;
    for (int i = 0; i < al->ntracks; i++) {
        if (al->tracks[i].disc > 1)
            multi = 1;
        if (al->tracks[i].number > maxn)
            maxn = al->tracks[i].number;
    }
    int w = 2;
    while (maxn >= 100) {
        w++;
        maxn /= 10;
    }
    for (int i = 0; i < al->ntracks; i++) {
        const struct Track *t = &al->tracks[i];
        if (multi && t->disc > 0)
            fprintf(fp, "  CD%d %0*d. %s", t->disc, w, t->number, t->title ? t->title : "");
        else
            fprintf(fp, "  %0*d. %s", w, t->number, t->title ? t->title : "");
        if (t->duration)
            fprintf(fp, "  [%s]", t->duration);
        fputc('\n', fp);
    }
}

int download_album(struct Album *al, const struct Options *opt, volatile int *abort_flag) {
    char fmt[32];
    if (prompt_format(al, opt, fmt, sizeof(fmt)) != 0)
        return -1;
    log_info("Album title: %s", al->title ? al->title : "?");
    log_verbose("Album year: %s", al->year ? al->year : "?");
    log_verbose("Album type: %s", al->type ? al->type : "?");
    log_info("Format: %s", fmt);

    if (opt->info_only) {
        print_album_info(al, stdout);
        return 0;
    }

    const char *outdir = ".";
    if (opt->output && *opt->output) {
        if (!path_is_dir(opt->output) && mkdir_p(opt->output) != 0) {
            log_error("Could not create output directory: %s", opt->output);
            log_info("Selecting current directory");
        } else {
            outdir = opt->output;
        }
    }
    char *folder = album_folder_name(al);
    char *dest = path_join(outdir, folder);
    free(folder);
    log_info("Output: %s", dest);

    if (!opt->cover_only) {
        log_info("Loading songs (this may take a while)");
        int64_t t0 = monotonic_ms();
        if (resolve_album_tracks(al, fmt, opt->threads, abort_flag) != 0) {
            free(dest);
            return -1;
        }
        log_info("Resolved download URLs in %.2fs", (monotonic_ms() - t0) / 1000.0);
    }

    if (opt->dry_run) {
        print_album_info(al, stdout);
        for (int i = 0; i < al->ntracks; i++) {
            if (al->tracks[i].download_url)
                fprintf(stdout, "  URL %s\n", al->tracks[i].download_url);
        }
        free(dest);
        return 0;
    }

    if (mkdir_p(dest) != 0) {
        log_error("Could not create directory: %s", dest);
        free(dest);
        return -1;
    }

    int multi = 0, maxn = 0;
    for (int i = 0; i < al->ntracks; i++) {
        if (al->tracks[i].disc > 1)
            multi = 1;
        if (al->tracks[i].number > maxn)
            maxn = al->tracks[i].number;
    }
    int w = 2;
    int tmp = maxn;
    while (tmp >= 100) {
        w++;
        tmp /= 10;
    }

    struct DlPool pool;
    memset(&pool, 0, sizeof(pool));
    pool.threads = opt->threads > 0 ? opt->threads : default_thread_count();
    pool.force = opt->force;
    pool.abort_flag = abort_flag;

    int cap = al->ncovers + al->ntracks + 4;
    pool.items = xcalloc((size_t)cap, sizeof(struct DlItem));

    if (!opt->no_cover) {
        for (int i = 0; i < al->ncovers; i++) {
            const char *url = al->covers[i].url;
            const char *slash = strrchr(url, '/');
            const char *base = slash ? slash + 1 : url;
            char *dec = url_decode(base);
            char *safe = filename_sanitize(dec && *dec ? dec : "cover");
            free(dec);
            /* ensure unique cover_N if name collides or is huge */
            char name[160];
            const char *dot = strrchr(safe, '.');
            const char *ext = (dot && dot != safe) ? dot : ".jpg";
            snprintf(name, sizeof(name), "cover_%02d%s", i + 1, ext);
            free(safe);
            struct DlItem *it = &pool.items[pool.nitems++];
            it->url = xstrdup(url);
            it->path = path_join(dest, name);
            it->label = xstrdup(name);
            it->state = DL_QUEUED;
        }
    }

    if (!opt->cover_only) {
        for (int i = 0; i < al->ntracks; i++) {
            struct Track *tr = &al->tracks[i];
            if (!tr->download_url)
                continue;
            char *fn = track_filename(tr, multi, w);
            struct DlItem *it = &pool.items[pool.nitems++];
            it->url = xstrdup(tr->download_url);
            it->path = path_join(dest, fn);
            it->label = fn;
            it->state = DL_QUEUED;
        }
    }

    if (pool.threads > pool.nitems && pool.nitems > 0)
        pool.threads = pool.nitems;
    log_dl("Downloading %d file%s with %d thread%s",
           pool.nitems, pool.nitems == 1 ? "" : "s",
           pool.threads, pool.threads == 1 ? "" : "s");

    int rc = run_pool(&pool);
    if (rc == 0)
        log_final("Album download complete");
    else
        log_error("Album download finished with errors");

    pool_free(&pool);
    free(dest);
    return rc;
}
