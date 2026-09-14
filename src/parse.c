#include "ghost.h"

#include <ctype.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *extract_b_after(const char *html, const char *label) {
    const char *p = html;
    while ((p = strstr(p, label))) {
        const char *q = p + strlen(label);
        while (*q && isspace((unsigned char)*q))
            q++;
        if (strncmp(q, "<b>", 3) == 0) {
            q += 3;
            const char *e = strstr(q, "</b>");
            if (!e)
                return NULL;
            return html_text(q, (size_t)(e - q));
        }
        /* Year: <b>2011</b> already handled. Some labels have no <b>. */
        p += strlen(label);
    }
    return NULL;
}

static char *extract_after_label(const char *html, const char *label, const char *stop) {
    const char *p = strstr(html, label);
    if (!p)
        return NULL;
    p += strlen(label);
    const char *e = strstr(p, stop ? stop : "<br");
    if (!e)
        e = p + strcspn(p, "\n");
    return html_text(p, (size_t)(e - p));
}

static void add_format(struct Album *al, const char *fmt) {
    if (!fmt || !*fmt)
        return;
    char *low = str_lower_dup(fmt);
    /* strip trailing punctuation */
    size_t n = strlen(low);
    while (n && (low[n - 1] == ',' || low[n - 1] == '.' || low[n - 1] == ')'))
        low[--n] = 0;
    char *s = low;
    while (*s == '(' || *s == '.')
        s++;
    if (!is_audio_format(s)) {
        free(low);
        return;
    }
    for (int i = 0; i < al->nformats; i++) {
        if (str_eq_ci(al->formats[i], s)) {
            free(low);
            return;
        }
    }
    al->formats = xrealloc(al->formats, (size_t)(al->nformats + 1) * sizeof(char *));
    al->formats[al->nformats++] = xstrdup(s);
    free(low);
}

static void parse_formats_from_header(const char *table, struct Album *al) {
    const char *hdr = strstr(table, "id=\"songlist_header\"");
    if (!hdr)
        hdr = table;
    const char *end = strstr(hdr, "</tr>");
    if (!end)
        return;
    const char *p = hdr;
    while (p < end) {
        const char *th = strstr(p, "<th");
        if (!th || th > end)
            break;
        const char *gt = strchr(th, '>');
        if (!gt || gt > end)
            break;
        gt++;
        const char *the = strstr(gt, "</th>");
        if (!the || the > end)
            break;
        char *text = html_text(gt, (size_t)(the - gt));
        add_format(al, text);
        free(text);
        p = the + 5;
    }
}

static void parse_formats_from_filesize(const char *html, struct Album *al) {
    const char *p = strstr(html, "Total Filesize:");
    if (!p)
        return;
    const char *br = strstr(p, "<br");
    const char *end = br ? br : p + 400;
    for (const char *q = p; q < end; q++) {
        if (*q == '(') {
            const char *r = q + 1;
            const char *c = strchr(r, ')');
            if (!c || c > end)
                break;
            char *fmt = xstrndup(r, (size_t)(c - r));
            add_format(al, fmt);
            free(fmt);
            q = c;
        }
    }
}

static int attr_has(const char *tag, const char *end, const char *key, const char *val) {
    /* look in opening tag [tag, first >] */
    const char *gt = strchr(tag, '>');
    if (!gt || (end && gt > end))
        return 0;
    char *open = xstrndup(tag, (size_t)(gt - tag));
    char *low = str_lower_dup(open);
    free(open);
    int hit = 0;
    if (val) {
        char pat[128];
        snprintf(pat, sizeof(pat), "%s=\"%s\"", key, val);
        hit = strstr(low, pat) != NULL;
        if (!hit) {
            snprintf(pat, sizeof(pat), "%s='%s'", key, val);
            hit = strstr(low, pat) != NULL;
        }
    } else {
        char pat[64];
        snprintf(pat, sizeof(pat), "%s=", key);
        hit = strstr(low, pat) != NULL;
    }
    free(low);
    return hit;
}

static char *td_inner(const char *td) {
    const char *gt = strchr(td, '>');
    if (!gt)
        return NULL;
    gt++;
    const char *end = strstr(gt, "</td>");
    if (!end)
        return NULL;
    return xstrndup(gt, (size_t)(end - gt));
}

static char *first_quoted_attr(const char *s, const char *name) {
    char pat[64];
    snprintf(pat, sizeof(pat), "%s=\"", name);
    const char *p = strcasestr(s, pat);
    char quote = '"';
    if (!p) {
        snprintf(pat, sizeof(pat), "%s='", name);
        p = strcasestr(s, pat);
        quote = '\'';
    }
    if (!p)
        return NULL;
    p += strlen(name) + 2;
    const char *e = strchr(p, quote);
    if (!e)
        return NULL;
    return xstrndup(p, (size_t)(e - p));
}

static char *first_href(const char *s) {
    return first_quoted_attr(s, "href");
}

static char *thumb_from_full(const char *url) {
    if (!url)
        return NULL;
    if (strstr(url, "/thumbs/"))
        return xstrdup(url);
    const char *slash = strrchr(url, '/');
    if (!slash || slash == url)
        return NULL;
    size_t dirlen = (size_t)(slash - url);
    char *out = xmalloc(dirlen + strlen(slash) + 16);
    memcpy(out, url, dirlen);
    sprintf(out + dirlen, "/thumbs%s", slash);
    return out;
}

static int parse_int_text(const char *s) {
    if (!s)
        return 0;
    while (*s && !isdigit((unsigned char)*s))
        s++;
    if (!*s)
        return 0;
    return atoi(s);
}

static void parse_tracks(const char *table, const char *page_url, struct Album *al) {
    const char *p = table;
    int cap = 0;
    while ((p = strstr(p, "<tr"))) {
        const char *tr_end = strstr(p, "</tr>");
        if (!tr_end)
            break;
        if (attr_has(p, tr_end, "id", "songlist_header") ||
            attr_has(p, tr_end, "id", "songlist_footer")) {
            p = tr_end + 5;
            continue;
        }

        struct Track tr;
        memset(&tr, 0, sizeof(tr));

        const char *td = p;
        while ((td = strstr(td, "<td")) && td < tr_end) {
            const char *td_end = strstr(td, "</td>");
            if (!td_end || td_end > tr_end)
                break;
            int is_center = attr_has(td, td_end, "align", "center");
            int is_right = attr_has(td, td_end, "align", "right");
            int is_click = 0;
            {
                const char *gt = strchr(td, '>');
                if (gt && gt < td_end) {
                    char *open = xstrndup(td, (size_t)(gt - td));
                    is_click = strstr(open, "clickable-row") != NULL;
                    free(open);
                }
            }

            char *inner = td_inner(td);
            char *text = inner ? html_text(inner, (size_t)-1) : xstrdup("");

            if (is_center && !is_click) {
                /* disc number if the cell is a bare integer */
                int all_digit = 0;
                if (text[0]) {
                    all_digit = 1;
                    for (char *c = text; *c; c++) {
                        if (!isdigit((unsigned char)*c)) {
                            all_digit = 0;
                            break;
                        }
                    }
                }
                if (all_digit)
                    tr.disc = atoi(text);
            } else if (is_right && !is_click && !tr.number) {
                tr.number = parse_int_text(text);
            } else if (is_click && !is_right && !tr.title) {
                tr.title = xstrdup(text);
                char *href = inner ? first_href(inner) : NULL;
                if (href) {
                    tr.page_url = url_join(page_url, href);
                    free(href);
                }
            } else if (is_click && is_right && !tr.duration && strchr(text, ':')) {
                tr.duration = xstrdup(text);
            }

            free(inner);
            free(text);
            td = td_end + 5;
        }

        if (tr.title && tr.page_url) {
            if (al->ntracks + 1 > cap) {
                cap = cap ? cap * 2 : 16;
                al->tracks = xrealloc(al->tracks, (size_t)cap * sizeof(struct Track));
            }
            al->tracks[al->ntracks++] = tr;
        } else {
            free(tr.title);
            free(tr.page_url);
            free(tr.duration);
        }
        p = tr_end + 5;
    }
}

static void parse_covers(const char *html, struct Album *al) {
    const char *p = html;
    int cap = 0;
    while ((p = strstr(p, "class=\"albumImage\""))) {
        /* find surrounding <div ...> then <a href */
        const char *div = p;
        while (div > html && strncmp(div, "<div", 4) != 0)
            div--;
        const char *end = strstr(p, "</div>");
        if (!end)
            break;
        char *href = first_href(div);
        char *src = first_quoted_attr(div, "src");
        if (href && (strncmp(href, "http://", 7) == 0 || strncmp(href, "https://", 8) == 0)) {
            if (al->ncovers + 1 > cap) {
                cap = cap ? cap * 2 : 4;
                al->covers = xrealloc(al->covers, (size_t)cap * sizeof(struct Cover));
            }
            al->covers[al->ncovers].url = href;
            if (src && (strncmp(src, "http://", 7) == 0 || strncmp(src, "https://", 8) == 0))
                al->covers[al->ncovers].thumb_url = src;
            else {
                al->covers[al->ncovers].thumb_url = thumb_from_full(href);
                free(src);
            }
            al->ncovers++;
        } else {
            free(href);
            free(src);
        }
        p = end + 6;
    }
}

int parse_album_page(const char *html, const char *page_url, struct Album *out) {
    memset(out, 0, sizeof(*out));
    if (!html)
        return -1;
    if (strstr(html, "No such album"))
        return -1;

    const char *content = strstr(html, "id=\"pageContent\"");
    if (!content)
        content = html;

    if (strstr(content, "id=\"songlist\"") == NULL &&
        strstr(content, "Download all songs at once:") == NULL) {
        /* might still be valid if table exists elsewhere */
        if (!strstr(html, "id=\"songlist\""))
            return -1;
    }

    char *h2 = html_extract(content, "<h2>", "</h2>");
    out->title = h2 ? html_text(h2, (size_t)-1) : xstrdup("Unknown Album");
    free(h2);

    out->year = extract_b_after(content, "Year:");
    out->type = extract_after_label(content, "Album type:", "<br");
    if (out->type) {
        /* Album type text may be "Soundtrack" already stripped */
        str_trim(out->type);
    }
    out->platforms = extract_after_label(content, "Platforms:", "<br");
    out->publisher = extract_after_label(content, "Published by:", "<br");
    out->date_added = extract_b_after(content, "Date Added:");
    char *nfiles = extract_b_after(content, "Number of Files:");
    if (nfiles) {
        out->nfiles = atoi(nfiles);
        free(nfiles);
    }
    char *fs = extract_after_label(content, "Total Filesize:", "<br");
    out->filesize = fs;

    char *desc = html_extract(content, "<h2>Description</h2>", "<h2>");
    if (desc) {
        out->description = html_text(desc, (size_t)-1);
        free(desc);
    }

    out->url = xstrdup(page_url ? page_url : "");
    out->id = slug_from_input(page_url);
    if (!out->id && page_url) {
        const char *slash = strrchr(page_url, '/');
        out->id = xstrdup(slash ? slash + 1 : page_url);
    }

    const char *table = strstr(html, "<table id=\"songlist\">");
    if (!table)
        table = strstr(html, "id=\"songlist\"");
    if (table) {
        const char *tend = strstr(table, "</table>");
        char *tbl = tend ? xstrndup(table, (size_t)(tend - table)) : xstrdup(table);
        parse_formats_from_header(tbl, out);
        parse_tracks(tbl, page_url ? page_url : GHOST_BASE_URL, out);
        free(tbl);
    }
    if (out->nformats == 0)
        parse_formats_from_filesize(content, out);
    if (out->nformats == 0)
        add_format(out, "mp3");

    parse_covers(content, out);

    if (out->ntracks == 0)
        return -1;
    return 0;
}

static char *cell_text_href_id(const char *td, char **id_out) {
    *id_out = NULL;
    char *href = first_href(td);
    if (href) {
        const char *key = "/game-soundtracks/album/";
        char *p = strstr(href, key);
        if (p) {
            p += strlen(key);
            char *q = strpbrk(p, "/?#");
            *id_out = q ? xstrndup(p, (size_t)(q - p)) : xstrdup(p);
        }
        free(href);
    }
    const char *gt = strchr(td, '>');
    if (!gt)
        return xstrdup("");
    const char *end = strstr(gt, "</td>");
    if (!end)
        return html_text(gt + 1, (size_t)-1);
    /* strip catalog span */
    char *inner = xstrndup(gt + 1, (size_t)(end - gt - 1));
    char *sp = strstr(inner, "<span");
    if (sp)
        *sp = 0;
    char *text = html_text(inner, (size_t)-1);
    free(inner);
    return text;
}

int parse_search_page(const char *html, const char *final_url, struct SearchResults *out) {
    memset(out, 0, sizeof(*out));
    if (!html)
        return -1;

    /* single-album redirect */
    if (final_url) {
        char *slug = slug_from_input(final_url);
        if (slug && strstr(final_url, "/game-soundtracks/album/")) {
            /* parse as album title from h2 */
            char *h2 = html_extract(html, "<h2>", "</h2>");
            out->items = xcalloc(1, sizeof(struct SearchHit));
            out->count = 1;
            out->items[0].id = slug;
            out->items[0].title = h2 ? html_text(h2, (size_t)-1) : xstrdup(slug);
            free(h2);
            return 0;
        }
        free(slug);
    }

    const char *table = strstr(html, "class=\"albumList\"");
    if (!table)
        table = strstr(html, "class='albumList'");
    if (!table)
        return 0;

    const char *tend = strstr(table, "</table>");
    const char *region_end = tend ? tend : table + strlen(table);
    const char *p = table;
    int cap = 0;
    int first = 1;
    while ((p = strstr(p, "<tr")) && p < region_end) {
        const char *tr_end = strstr(p, "</tr>");
        if (!tr_end || tr_end > region_end)
            break;
        if (first) {
            first = 0;
            p = tr_end + 5;
            continue; /* header */
        }
        char *cells[8] = {0};
        char *ids[8] = {0};
        int nc = 0;
        const char *td = p;
        while ((td = strstr(td, "<td")) && td < tr_end && nc < 8) {
            const char *td_end = strstr(td, "</td>");
            if (!td_end || td_end > tr_end)
                break;
            char *chunk = xstrndup(td, (size_t)(td_end - td + 5));
            cells[nc] = cell_text_href_id(chunk, &ids[nc]);
            free(chunk);
            nc++;
            td = td_end + 5;
        }
        if (nc >= 2 && (ids[1] || ids[0])) {
            if (out->count + 1 > cap) {
                cap = cap ? cap * 2 : 8;
                out->items = xrealloc(out->items, (size_t)cap * sizeof(struct SearchHit));
            }
            struct SearchHit *h = &out->items[out->count++];
            memset(h, 0, sizeof(*h));
            if (ids[1]) {
                h->id = ids[1];
                ids[1] = NULL;
            } else {
                h->id = ids[0];
                ids[0] = NULL;
            }
            h->title = cells[1] ? cells[1] : xstrdup(h->id);
            cells[1] = NULL;
            h->platforms = cells[2];
            cells[2] = NULL;
            h->type = cells[3];
            cells[3] = NULL;
            h->year = cells[4];
            cells[4] = NULL;
        }
        for (int i = 0; i < nc; i++) {
            free(cells[i]);
            free(ids[i]);
        }
        p = tr_end + 5;
    }
    return 0;
}

static int href_is_audio(const char *href, const char *want_ext) {
    if (!href)
        return 0;
    if (strncmp(href, "http://", 7) != 0 && strncmp(href, "https://", 8) != 0)
        return 0;
    char *q = xstrdup(href);
    char *hash = strpbrk(q, "?#");
    if (hash)
        *hash = 0;
    int ok = 0;
    if (want_ext && *want_ext) {
        char suf[16];
        snprintf(suf, sizeof(suf), ".%s", want_ext);
        ok = str_ends_ci(q, suf);
    } else {
        static const char *exts[] = {
            ".flac", ".wav", ".aiff", ".alac", ".ape", ".ogg", ".opus",
            ".aac", ".m4a", ".wma", ".mp3", NULL
        };
        for (int i = 0; exts[i]; i++) {
            if (str_ends_ci(q, exts[i])) {
                ok = 1;
                break;
            }
        }
    }
    free(q);
    return ok;
}

int parse_song_page(const char *html, const char *want_ext, char **download_url) {
    *download_url = NULL;
    if (!html)
        return -1;

    /* Prefer songDownloadLink anchors */
    const char *p = html;
    char *fallback = NULL;
    while ((p = strstr(p, "<a "))) {
        const char *end = strchr(p, '>');
        if (!end)
            break;
        char *href = first_href(p);
        if (href && href_is_audio(href, want_ext)) {
            /* songDownloadLink is the canonical "Click here to download as FMT" */
            const char *close = strstr(p, "</a>");
            int preferred = 0;
            if (close) {
                char *inner = xstrndup(p, (size_t)(close - p));
                if (strstr(inner, "songDownloadLink") || strstr(inner, "Click here to download"))
                    preferred = 1;
                free(inner);
            }
            if (preferred) {
                *download_url = href;
                free(fallback);
                return 0;
            }
            if (!fallback)
                fallback = href;
            else
                free(href);
        } else {
            free(href);
        }
        p = end + 1;
    }
    if (fallback) {
        *download_url = fallback;
        return 0;
    }
    return -1;
}

int fetch_album(const char *input, struct Album *out) {
    memset(out, 0, sizeof(*out));
    char *slug = slug_from_input(input);
    if (!slug) {
        log_error("Not a KHInsider album URL or slug: %s", input);
        return -1;
    }
    char url[1024];
    snprintf(url, sizeof(url), "%s/game-soundtracks/album/%s", GHOST_BASE_URL, slug);
    log_info("Loading webpage");
    char *body = NULL, *final_url = NULL;
    size_t len = 0;
    int64_t t0 = monotonic_ms();
    int rc = http_get(url, &body, &len, &final_url);
    if (rc != 0 || !body) {
        log_error("Failed to fetch album page (%s)", slug);
        free(slug);
        free(body);
        free(final_url);
        return -1;
    }
    if (strstr(body, "No such album")) {
        log_error("Album not found: %s", slug);
        free(body);
        free(final_url);
        free(slug);
        return -1;
    }
    rc = parse_album_page(body, final_url ? final_url : url, out);
    free(body);
    free(final_url);
    if (rc != 0) {
        log_error("Could not parse album page (%s). The site layout may have changed.", slug);
        album_free(out);
        free(slug);
        return -1;
    }
    if (!out->id)
        out->id = slug;
    else
        free(slug);
    log_info("Loaded \"%s\" in %.2fs", out->title ? out->title : out->id,
             (monotonic_ms() - t0) / 1000.0);
    return 0;
}

int fetch_search(const char *query, struct SearchResults *out) {
    memset(out, 0, sizeof(*out));
    if (!query || !*query)
        return -1;
    char *enc = url_encode(query);
    char url[2048];
    snprintf(url, sizeof(url), "%s/search?search=%s", GHOST_BASE_URL, enc);
    free(enc);
    char *body = NULL, *final_url = NULL;
    size_t len = 0;
    if (http_get(url, &body, &len, &final_url) != 0 || !body) {
        log_error("Search request failed");
        free(body);
        free(final_url);
        return -1;
    }
    int rc = parse_search_page(body, final_url, out);
    free(body);
    free(final_url);
    return rc;
}

int resolve_track_url(struct Track *tr, const char *fmt) {
    if (!tr || !tr->page_url)
        return -1;
    char *body = NULL;
    if (http_get(tr->page_url, &body, NULL, NULL) != 0 || !body) {
        free(body);
        return -1;
    }
    char *url = NULL;
    int rc = parse_song_page(body, fmt, &url);
    free(body);
    if (rc != 0 || !url)
        return -1;
    free(tr->download_url);
    tr->download_url = url;
    free(tr->ext);
    tr->ext = xstrdup(fmt ? fmt : "mp3");
    return 0;
}

struct ResolveState {
    struct Album *al;
    const char *fmt;
    volatile int *abort_flag;
    int next;
    int failed;
    pthread_mutex_t mu;
};

static void *resolve_worker(void *arg) {
    struct ResolveState *st = arg;
    for (;;) {
        if (st->abort_flag && *st->abort_flag)
            return NULL;
        pthread_mutex_lock(&st->mu);
        int i = st->next++;
        pthread_mutex_unlock(&st->mu);
        if (i >= st->al->ntracks)
            return NULL;
        int tries = 0;
        int ok = 0;
        while (tries < 3) {
            if (resolve_track_url(&st->al->tracks[i], st->fmt) == 0) {
                ok = 1;
                break;
            }
            tries++;
        }
        if (!ok) {
            pthread_mutex_lock(&st->mu);
            st->failed++;
            pthread_mutex_unlock(&st->mu);
        }
    }
}

int resolve_album_tracks(struct Album *al, const char *fmt, int threads, volatile int *abort_flag) {
    if (!al || al->ntracks <= 0)
        return -1;
    if (threads < 1)
        threads = 1;
    if (threads > al->ntracks)
        threads = al->ntracks;

    struct ResolveState st;
    memset(&st, 0, sizeof(st));
    st.al = al;
    st.fmt = fmt;
    st.abort_flag = abort_flag;
    pthread_mutex_init(&st.mu, NULL);

    pthread_t *th = xcalloc((size_t)threads, sizeof(pthread_t));
    int started = 0;
    for (int i = 0; i < threads; i++) {
        if (pthread_create(&th[i], NULL, resolve_worker, &st) != 0)
            break;
        started++;
    }
    for (int i = 0; i < started; i++)
        pthread_join(th[i], NULL);
    free(th);
    pthread_mutex_destroy(&st.mu);

    int missing = 0;
    for (int i = 0; i < al->ntracks; i++) {
        if (!al->tracks[i].download_url)
            missing++;
    }
    if (missing)
        log_error("Failed to resolve %d / %d track download URLs", missing, al->ntracks);
    return missing == al->ntracks ? -1 : 0;
}
