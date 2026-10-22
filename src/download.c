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
    int is_track;
    enum DlState state;
    int64_t total;
    int64_t have;
    int reported;
};

struct DlPool {
    struct DlItem *items;
    int nitems;
    int threads;
    int next;
    int force;
    const char *referer;
    volatile int *abort_flag;
    pthread_mutex_t mu;
    int64_t start_ms;
    int64_t last_have;
    int64_t last_ms;
    double speed;
};

struct PoolStats {
    int done, fail, skip, active;
    int64_t have;      /* bytes on disk so far */
    int64_t est_total; /* known sizes plus an estimate for unknown ones */
    int64_t fetched;   /* bytes transferred in this run (excludes skips) */
};

static int term_width(void) {
#ifdef TIOCGWINSZ
    struct winsize ws;
    if (ioctl(STDERR_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col >= 20)
        return ws.ws_col;
#endif
    const char *e = getenv("COLUMNS");
    if (e && atoi(e) > 20)
        return atoi(e);
    return 80;
}

static void bar_fill(char *buf, int width, double frac) {
    if (width < 1) {
        buf[0] = 0;
        return;
    }
    if (frac < 0)
        frac = 0;
    if (frac > 1)
        frac = 1;
    int filled = (int)(frac * width + 0.5);
    if (filled > width)
        filled = width;
    for (int i = 0; i < width; i++)
        buf[i] = (i < filled) ? '#' : '-';
    buf[width] = 0;
}

/* Shorten S in place to at most MAXCOLS characters (UTF-8 aware), adding "...". */
static void utf8_ellipsize(char *s, int maxcols) {
    if (maxcols < 4)
        maxcols = 4;
    int cols = 0;
    size_t cut3 = 0; /* byte offset where the text must stop to fit "..." */
    for (size_t i = 0; s[i]; i++) {
        if (((unsigned char)s[i] & 0xC0) == 0x80)
            continue;
        if (cols == maxcols - 3)
            cut3 = i;
        if (++cols > maxcols) {
            memcpy(s + cut3, "...", 4);
            return;
        }
    }
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

static void pool_stats(const struct DlPool *p, struct PoolStats *st) {
    memset(st, 0, sizeof(*st));
    int64_t known = 0;
    int nknown = 0, nunknown = 0;
    for (int i = 0; i < p->nitems; i++) {
        const struct DlItem *it = &p->items[i];
        enum DlState s = it->state;
        int64_t have = it->have, total = it->total;
        st->have += have;
        if (s != DL_SKIP)
            st->fetched += have;
        if (s == DL_DONE)
            st->done++;
        else if (s == DL_FAIL)
            st->fail++;
        else if (s == DL_SKIP)
            st->skip++;
        else if (s == DL_ACTIVE)
            st->active++;
        if (total > 0) {
            known += total;
            nknown++;
        } else if (s == DL_DONE || s == DL_SKIP) {
            known += have;
            nknown++;
        } else if (s != DL_FAIL) {
            nunknown++;
        }
    }
    st->est_total = known;
    if (nunknown > 0 && nknown > 0)
        st->est_total += known / nknown * nunknown;
}

static void draw_progress(struct DlPool *p, int *last_rows) {
    if (ghost_log_quiet() || !ghost_is_tty(stderr))
        return;

    struct PoolStats st;
    pool_stats(p, &st);
    int finished = st.done + st.fail + st.skip;
    double frac = p->nitems > 0 ? (double)finished / (double)p->nitems : 0;
    if (st.est_total > 0)
        frac = (double)st.have / (double)st.est_total;
    if (finished >= p->nitems)
        frac = 1;

    int64_t now = monotonic_ms();
    if (now - p->last_ms >= 100) {
        double inst = (double)(st.fetched - p->last_have) * 1000.0 / (double)(now - p->last_ms);
        if (inst < 0)
            inst = 0;
        p->speed = p->speed > 0 ? p->speed * 0.8 + inst * 0.2 : inst;
        p->last_have = st.fetched;
        p->last_ms = now;
    }

    double elapsed = (now - p->start_ms) / 1000.0;
    double eta = -1;
    if (finished >= p->nitems)
        eta = 0;
    else if (p->speed > 1 && st.est_total > st.have)
        eta = (double)(st.est_total - st.have) / p->speed;

    int tw = term_width();
    int color = ghost_color_enabled();

    char *block = xmalloc(16384);
    size_t used = 0;
    int rows = 0;
    #define APP(fmt, ...) do { \
        int _n = snprintf(block + used, 16384 - used, fmt, ##__VA_ARGS__); \
        if (_n > 0) used += (size_t)_n; \
        if (used >= 16384) used = 16383; \
    } while (0)

    /* Autowrap off: long lines get clipped instead of breaking the redraw. */
    APP("\033[?7l");
    if (*last_rows > 0)
        APP("\r\033[%dA\033[J", *last_rows);
    else
        APP("\r\033[J");

    const char *mag = color ? "\033[38;2;255;0;255m" : "";
    const char *cyan = color ? "\033[38;2;0;242;255m" : "";
    const char *rst = color ? "\033[0m" : "";

    int barw = tw >= 100 ? 28 : (tw >= 80 ? 20 : 10);
    int namew = tw - (barw + 43);
    if (namew < 12)
        namew = 12;
    if (namew > 60)
        namew = 60;

    int shown = 0;
    for (int i = 0; i < p->nitems && shown < p->threads; i++) {
        struct DlItem *it = &p->items[i];
        if (it->state != DL_ACTIVE)
            continue;
        int64_t have = it->have, total = it->total;
        char name[512];
        snprintf(name, sizeof(name), "%s", it->label ? it->label : "");
        utf8_ellipsize(name, namew);
        if (total <= 0) {
            APP("  %s%s%s  %s  starting...\n", mag, state_label(it->state), rst, name);
        } else {
            double f = (double)have / (double)total;
            char b[64], hs[32], ht[32];
            bar_fill(b, barw, f);
            human_bytes(hs, sizeof(hs), have);
            human_bytes(ht, sizeof(ht), total);
            APP("  %s%s%s  %-*s  [%s] %5.1f%%  %s / %s\n", mag, state_label(it->state), rst,
                namew, name, b, f * 100.0, hs, ht);
        }
        rows++;
        shown++;
    }

    char ob[128], hh[32], htt[32], hr[32], he[32], het[32];
    int obw = tw - 20;
    if (obw > 60)
        obw = 60;
    if (obw < 10)
        obw = 10;
    bar_fill(ob, obw, frac);
    human_bytes(hh, sizeof(hh), st.have);
    human_bytes(htt, sizeof(htt), st.est_total);
    human_rate(hr, sizeof(hr), p->speed);
    human_duration(he, sizeof(he), elapsed);
    human_duration(het, sizeof(het), eta);

    APP("%s  [%s]%s %5.1f%%\n", cyan, ob, rst, frac * 100.0);
    rows++;
    APP("  %d/%d files  %s / %s%s  %s  elapsed %s  ETA %s", finished, p->nitems, hh,
        (finished < p->nitems && st.est_total > 0) ? "~" : "", htt, hr, he, het);
    if (st.fail)
        APP("  fail %d", st.fail);
    if (st.skip)
        APP("  skip %d", st.skip);
    APP("\n\033[?7h");
    rows++;

    fwrite(block, 1, used, stderr);
    fflush(stderr);
    *last_rows = rows;
    free(block);
#undef APP
}

/* Without a TTY, print one line per finished file instead of a live display. */
static void report_plain(struct DlPool *p) {
    if (ghost_log_quiet() || ghost_is_tty(stderr) || (p->abort_flag && *p->abort_flag))
        return;
    for (int i = 0; i < p->nitems; i++) {
        struct DlItem *it = &p->items[i];
        enum DlState s = it->state;
        if (it->reported || (s != DL_DONE && s != DL_FAIL && s != DL_SKIP))
            continue;
        it->reported = 1;
        int finished = 0;
        for (int j = 0; j < p->nitems; j++) {
            if (p->items[j].reported)
                finished++;
        }
        char hs[32];
        human_bytes(hs, sizeof(hs), it->have);
        if (s == DL_FAIL)
            log_error("[%d/%d] failed: %s", finished, p->nitems, it->label);
        else
            log_dl("[%d/%d] %s %s (%s)", finished, p->nitems,
                   s == DL_SKIP ? "exists" : "saved", it->label, hs);
    }
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
            FILE *fp = fopen(it->path, "rb");
            if (fp) {
                fseek(fp, 0, SEEK_END);
                it->have = it->total = (int64_t)ftell(fp);
                fclose(fp);
            }
            it->state = DL_SKIP;
            continue;
        }
        it->state = DL_ACTIVE;
        int64_t total = 0;
        int rc = http_download(it->url, it->path, p->referer, item_progress, it, &total,
                               p->abort_flag);
        if (rc == 0) {
            if (total > 0)
                it->have = it->total = total;
            it->state = DL_DONE;
        } else {
            it->state = DL_FAIL;
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
    if (started == 0) {
        log_error("Could not start download threads");
        free(th);
        pthread_mutex_destroy(&p->mu);
        return -1;
    }

    int last_rows = 0;
    int live = ghost_is_tty(stderr) && !ghost_log_quiet();
    if (live)
        fputs("\033[?25l", stderr);

    for (;;) {
        struct PoolStats st;
        pool_stats(p, &st);
        if (st.done + st.fail + st.skip >= p->nitems)
            break;
        if (p->abort_flag && *p->abort_flag)
            break;
        draw_progress(p, &last_rows);
        report_plain(p);
        usleep(120000);
    }

    for (int i = 0; i < started; i++)
        pthread_join(th[i], NULL);
    free(th);
    pthread_mutex_destroy(&p->mu);

    draw_progress(p, &last_rows);
    report_plain(p);
    if (live)
        fputs("\033[?25h", stderr);

    struct PoolStats st;
    pool_stats(p, &st);
    if (p->abort_flag && *p->abort_flag)
        return -1;

    for (int i = 0; i < p->nitems; i++) {
        if (p->items[i].state == DL_FAIL && live)
            log_error("Failed: %s", p->items[i].label);
    }
    double secs = (monotonic_ms() - p->start_ms) / 1000.0;
    char hb[32], hr[32], hd[32];
    human_bytes(hb, sizeof(hb), st.fetched);
    human_rate(hr, sizeof(hr), secs > 0 ? st.fetched / secs : 0);
    human_duration(hd, sizeof(hd), secs);
    log_info("%d downloaded, %d already present, %d failed - %s in %s (%s)",
             st.done, st.skip, st.fail, hb, hd, hr);
    return st.fail ? -1 : 0;
}

static void pool_free(struct DlPool *p) {
    for (int i = 0; i < p->nitems; i++) {
        free(p->items[i].url);
        free(p->items[i].path);
        free(p->items[i].label);
    }
    free(p->items);
}

static void print_format_list(const struct Album *al, const char *picked) {
    for (int i = 0; i < al->nformats; i++) {
        int def = picked && str_eq_ci(al->formats[i], picked);
        fprintf(stderr, "  %d) %s%s\n", i + 1, al->formats[i], def ? " (Default)" : "");
    }
}

int prompt_format(const struct Album *al, const struct Options *opt, char *out, size_t outsz) {
    const char *best = pick_format(al->formats, al->nformats, NULL, 1);
    if (opt->format && *opt->format) {
        const char *hit = match_format(al->formats, al->nformats, opt->format);
        if (hit) {
            snprintf(out, outsz, "%s", hit);
            return 0;
        }
        char avail[256] = "";
        for (int i = 0; i < al->nformats; i++) {
            size_t L = strlen(avail);
            snprintf(avail + L, sizeof(avail) - L, "%s%s", i ? ", " : "", al->formats[i]);
        }
        log_error("Format \"%s\" is not available for this album (available: %s)",
                  opt->format, avail);
    }
    if (opt->use_default || !ghost_is_tty(stdin) || ghost_log_quiet() || al->nformats <= 1) {
        if (opt->format && *opt->format)
            log_info("Using %s instead", best);
        snprintf(out, outsz, "%s", best);
        return 0;
    }
    log_input("Choose a format: enter a number or name, Return for the default");
    print_format_list(al, best);
    for (;;) {
        fprintf(stderr, ": ");
        fflush(stderr);
        char line[64];
        if (!fgets(line, sizeof(line), stdin)) {
            clearerr(stdin);
            return -1;
        }
        char *t = str_trim(line);
        if (!*t) {
            snprintf(out, outsz, "%s", best);
            return 0;
        }
        const char *hit = match_format(al->formats, al->nformats, t);
        if (hit) {
            snprintf(out, outsz, "%s", hit);
            return 0;
        }
        char *end = NULL;
        long v = strtol(t, &end, 10);
        if (end != t && !*end && v >= 1 && v <= al->nformats) {
            snprintf(out, outsz, "%s", al->formats[v - 1]);
            return 0;
        }
        log_error("Not a valid choice");
        print_format_list(al, best);
    }
}

static void track_layout(const struct Album *al, int *multi, int *width) {
    int m = 0, maxn = 0;
    for (int i = 0; i < al->ntracks; i++) {
        if (al->tracks[i].disc > 1)
            m = 1;
        if (al->tracks[i].number > maxn)
            maxn = al->tracks[i].number;
    }
    int w = 2;
    while (maxn >= 100) {
        w++;
        maxn /= 10;
    }
    *multi = m;
    *width = w;
}

void print_album_info(const struct Album *al, FILE *fp) {
    fprintf(fp, "Title:      %s\n", al->title ? al->title : "-");
    fprintf(fp, "ID:         %s\n", al->id ? al->id : "-");
    fprintf(fp, "URL:        %s\n", al->url ? al->url : "-");
    fprintf(fp, "Year:       %s\n", al->year && *al->year ? al->year : "-");
    fprintf(fp, "Type:       %s\n", al->type && *al->type ? al->type : "-");
    fprintf(fp, "Platforms:  %s\n", al->platforms && *al->platforms ? al->platforms : "-");
    fprintf(fp, "Publisher:  %s\n", al->publisher && *al->publisher ? al->publisher : "-");
    fprintf(fp, "Date added: %s\n", al->date_added && *al->date_added ? al->date_added : "-");
    fprintf(fp, "Files:      %d\n", al->ntracks);
    fprintf(fp, "Filesize:   %s\n", al->filesize && *al->filesize ? al->filesize : "-");
    fprintf(fp, "Covers:     %d\n", al->ncovers);
    fprintf(fp, "Formats:    ");
    for (int i = 0; i < al->nformats; i++)
        fprintf(fp, "%s%s", al->formats[i], (i + 1 < al->nformats) ? ", " : "");
    fprintf(fp, "\n");
    if (al->description && al->description[0])
        fprintf(fp, "Description:\n  %s\n", al->description);
    fprintf(fp, "\nTracks (# is the index used by --tracks):\n");
    int multi, w;
    track_layout(al, &multi, &w);
    int iw = al->ntracks >= 100 ? 3 : (al->ntracks >= 10 ? 2 : 1);
    for (int i = 0; i < al->ntracks; i++) {
        const struct Track *t = &al->tracks[i];
        fprintf(fp, "  %*d  ", iw, i + 1);
        if (multi && t->disc > 0)
            fprintf(fp, "CD%d ", t->disc);
        if (t->number > 0)
            fprintf(fp, "%0*d. ", w, t->number);
        fprintf(fp, "%s", t->title ? t->title : "");
        if (t->duration)
            fprintf(fp, "  [%s]", t->duration);
        fputc('\n', fp);
    }
}

/* Keep only the tracks selected by SPEC (1-based list positions). */
static int filter_tracks(struct Album *al, const char *spec) {
    unsigned char *sel = xcalloc((size_t)al->ntracks + 1, 1);
    if (parse_track_selection(spec, al->ntracks, sel) != 0) {
        log_error("Invalid --tracks value \"%s\" (album has %d tracks; e.g. 1-5,8,12-)",
                  spec, al->ntracks);
        free(sel);
        return -1;
    }
    int j = 0;
    for (int i = 0; i < al->ntracks; i++) {
        if (sel[i]) {
            al->tracks[j++] = al->tracks[i];
        } else {
            struct Track *t = &al->tracks[i];
            free(t->title);
            free(t->page_url);
            free(t->download_url);
            free(t->ext);
            free(t->duration);
            free(t->size_str);
        }
    }
    free(sel);
    if (j == 0) {
        log_error("--tracks \"%s\" selects no tracks", spec);
        al->ntracks = 0;
        return -1;
    }
    log_info("Selected %d of %d tracks", j, al->ntracks);
    al->ntracks = j;
    return 0;
}

static int path_taken(const struct DlPool *p, const char *path) {
    for (int i = 0; i < p->nitems; i++) {
        if (str_eq_ci(p->items[i].path, path))
            return 1;
    }
    return 0;
}

/* Add an item, renaming "name.ext" to "name (2).ext" if another item has that path. */
static void pool_add(struct DlPool *p, const char *dest, const char *url, const char *fname,
                     int is_track) {
    char *name = xstrdup(fname);
    char *path = path_join(dest, name);
    for (int n = 2; path_taken(p, path); n++) {
        const char *dot = strrchr(fname, '.');
        size_t stem = dot ? (size_t)(dot - fname) : strlen(fname);
        free(name);
        free(path);
        name = xmalloc(strlen(fname) + 16);
        sprintf(name, "%.*s (%d)%s", (int)stem, fname, n, dot ? dot : "");
        path = path_join(dest, name);
    }
    struct DlItem *it = &p->items[p->nitems++];
    it->url = xstrdup(url);
    it->path = path;
    it->label = name;
    it->is_track = is_track;
    it->state = DL_QUEUED;
}

static void write_playlist(const struct DlPool *p, const char *dest, const struct Album *al) {
    char *path = path_join(dest, "playlist.m3u8");
    FILE *fp = fopen(path, "w");
    if (!fp) {
        log_error("Could not write %s", path);
        free(path);
        return;
    }
    fprintf(fp, "#EXTM3U\n");
    if (al->title)
        fprintf(fp, "#PLAYLIST:%s\n", al->title);
    for (int i = 0; i < p->nitems; i++) {
        const struct DlItem *it = &p->items[i];
        if (it->is_track && (it->state == DL_DONE || it->state == DL_SKIP))
            fprintf(fp, "%s\n", it->label);
    }
    fclose(fp);
    log_verbose("Wrote %s", path);
    free(path);
}

int download_album(struct Album *al, const struct Options *opt, volatile int *abort_flag) {
    /* Name files from the full album so partial and full downloads agree. */
    int multi, w;
    track_layout(al, &multi, &w);
    if (opt->tracks && *opt->tracks && filter_tracks(al, opt->tracks) != 0)
        return -1;

    char fmt[32] = "";
    if (!opt->cover_only) {
        if (prompt_format(al, opt, fmt, sizeof(fmt)) != 0)
            return -1;
    }
    log_info("Album: %s", al->title ? al->title : "?");
    log_verbose("Album year: %s", al->year ? al->year : "?");
    log_verbose("Album type: %s", al->type ? al->type : "?");
    if (fmt[0])
        log_info("Format: %s", fmt);

    const char *outdir = ".";
    if (opt->output && *opt->output) {
        if (!opt->dry_run && !path_is_dir(opt->output) && mkdir_p(opt->output) != 0) {
            log_error("Could not create output directory: %s", opt->output);
            return -1;
        }
        outdir = opt->output;
    }
    char *folder = album_folder_name(al);
    char *dest = path_join(outdir, folder);
    free(folder);
    log_info("Output: %s", dest);

    if (!opt->cover_only) {
        int64_t t0 = monotonic_ms();
        if (resolve_album_tracks(al, fmt, opt->threads, abort_flag) != 0) {
            free(dest);
            return -1;
        }
        log_verbose("Resolved download URLs in %.2fs", (monotonic_ms() - t0) / 1000.0);
    }

    struct DlPool pool;
    memset(&pool, 0, sizeof(pool));
    pool.threads = opt->threads > 0 ? opt->threads : default_thread_count();
    pool.force = opt->force;
    pool.referer = al->url;
    pool.abort_flag = abort_flag;
    pool.items = xcalloc((size_t)(al->ncovers + al->ntracks + 1), sizeof(struct DlItem));

    if (!opt->no_cover) {
        for (int i = 0; i < al->ncovers; i++) {
            const char *url = al->covers[i].url;
            const char *slash = strrchr(url, '/');
            const char *dot = strrchr(slash ? slash : url, '.');
            char ext[8] = ".jpg";
            if (dot && strlen(dot) >= 2 && strlen(dot) <= 5) {
                int ok = 1;
                for (const char *c = dot + 1; *c; c++) {
                    if (!(*c >= 'a' && *c <= 'z') && !(*c >= 'A' && *c <= 'Z') &&
                        !(*c >= '0' && *c <= '9'))
                        ok = 0;
                }
                if (ok)
                    snprintf(ext, sizeof(ext), "%s", dot);
            }
            char name[32];
            snprintf(name, sizeof(name), "cover_%02d%s", i + 1, ext);
            pool_add(&pool, dest, url, name, 0);
        }
    }

    if (!opt->cover_only) {
        for (int i = 0; i < al->ntracks; i++) {
            struct Track *tr = &al->tracks[i];
            if (!tr->download_url)
                continue;
            char *fn = track_filename(tr, multi, w);
            pool_add(&pool, dest, tr->download_url, fn, 1);
            free(fn);
        }
    }

    if (opt->dry_run) {
        printf("%s/\n", dest);
        for (int i = 0; i < pool.nitems; i++)
            printf("  %s\n    <- %s\n", pool.items[i].label, pool.items[i].url);
        pool_free(&pool);
        free(dest);
        return 0;
    }

    if (pool.nitems == 0) {
        log_error("Nothing to download");
        pool_free(&pool);
        free(dest);
        return -1;
    }

    if (mkdir_p(dest) != 0) {
        log_error("Could not create directory: %s", dest);
        pool_free(&pool);
        free(dest);
        return -1;
    }

    if (pool.threads > pool.nitems)
        pool.threads = pool.nitems;
    log_dl("Downloading %d file%s with %d thread%s",
           pool.nitems, pool.nitems == 1 ? "" : "s",
           pool.threads, pool.threads == 1 ? "" : "s");

    int rc = run_pool(&pool);
    if (opt->playlist && !opt->cover_only && !(abort_flag && *abort_flag))
        write_playlist(&pool, dest, al);
    if (abort_flag && *abort_flag)
        log_error("Download interrupted; run the same command again to resume");
    else if (rc == 0)
        log_final("Album download complete");
    else
        log_error("Album download finished with errors; run again to retry failed files");

    pool_free(&pool);
    free(dest);
    return rc;
}
