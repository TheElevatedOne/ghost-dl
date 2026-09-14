#include "ghost.h"

#ifdef HAVE_NCURSES
#include <locale.h>
#include <ncurses.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

enum TuiFocus { FOCUS_SEARCH, FOCUS_LIST };

struct Tui {
    struct Options *opt;
    volatile int *abort_flag;
    char query[256];
    int qlen;
    struct SearchResults results;
    int selected;
    int scroll;
    struct Album album;
    int album_loaded;
    int album_sel; /* which result the album belongs to */
    char status[256];
    char fmt[32];
    enum TuiFocus focus;
    int running;
    struct TermImg cover;
    int cover_shown;
    int cover_row;
    int cover_col;
    int cover_cols;
    int cover_rows;
};

static void set_status(struct Tui *t, const char *s) {
    snprintf(t->status, sizeof(t->status), "%s", s ? s : "");
}

static void draw_box_title(int y, int x, int h, int w, const char *title) {
    attron(COLOR_PAIR(1));
    mvaddch(y, x, ACS_ULCORNER);
    mvaddch(y, x + w - 1, ACS_URCORNER);
    mvaddch(y + h - 1, x, ACS_LLCORNER);
    mvaddch(y + h - 1, x + w - 1, ACS_LRCORNER);
    for (int i = 1; i < w - 1; i++) {
        mvaddch(y, x + i, ACS_HLINE);
        mvaddch(y + h - 1, x + i, ACS_HLINE);
    }
    for (int i = 1; i < h - 1; i++) {
        mvaddch(y + i, x, ACS_VLINE);
        mvaddch(y + i, x + w - 1, ACS_VLINE);
    }
    attroff(COLOR_PAIR(1));
    if (title && *title) {
        attron(COLOR_PAIR(2) | A_BOLD);
        mvaddnstr(y, x + 2, title, w - 4);
        attroff(COLOR_PAIR(2) | A_BOLD);
    }
}

static void put_trunc(int y, int x, int maxw, const char *s, int attrs) {
    if (!s)
        s = "";
    attron(attrs);
    mvaddnstr(y, x, s, maxw);
    attroff(attrs);
}

static void album_clear(struct Tui *t) {
    if (t->cover_shown)
        termimg_clear();
    termimg_free(&t->cover);
    t->cover_shown = 0;
    t->cover_row = t->cover_col = t->cover_cols = t->cover_rows = 0;
    album_free(&t->album);
    t->album_loaded = 0;
    t->album_sel = -1;
}

static void do_search(struct Tui *t) {
    search_free(&t->results);
    t->selected = 0;
    t->scroll = 0;
    album_clear(t);
    if (t->qlen == 0) {
        set_status(t, "Type a search and press Enter");
        return;
    }
    set_status(t, "Searching...");
    /* paint status immediately */
    mvaddnstr(LINES - 2, 2, t->status, COLS - 4);
    refresh();
    int q = ghost_log_quiet(), v = ghost_log_verbose();
    ghost_log_init(1, 0);
    int frc = fetch_search(t->query, &t->results);
    ghost_log_init(q, v);
    if (frc != 0) {
        set_status(t, "Search failed");
        return;
    }
    if (t->results.count == 0)
        set_status(t, "No albums found");
    else {
        snprintf(t->status, sizeof(t->status), "Found %d album%s",
                 t->results.count, t->results.count == 1 ? "" : "s");
    }
}

static void do_lookup(struct Tui *t) {
    if (t->selected < 0 || t->selected >= t->results.count)
        return;
    if (t->album_loaded && t->album_sel == t->selected)
        return;
    album_clear(t);
    set_status(t, "Loading album...");
    mvaddnstr(LINES - 2, 2, t->status, COLS - 4);
    refresh();
    struct SearchHit *h = &t->results.items[t->selected];
    int q = ghost_log_quiet(), v = ghost_log_verbose();
    ghost_log_init(1, 0);
    int frc = fetch_album(h->id, &t->album);
    ghost_log_init(q, v);
    if (frc != 0) {
        set_status(t, "Failed to load album");
        return;
    }
    t->album_loaded = 1;
    t->album_sel = t->selected;
    const char *picked = pick_format(t->album.formats, t->album.nformats,
                                     t->opt->format, 1);
    snprintf(t->fmt, sizeof(t->fmt), "%s", picked);
    if (termimg_supported() && t->album.ncovers > 0) {
        set_status(t, "Loading cover...");
        mvaddnstr(LINES - 2, 2, t->status, COLS - 4);
        refresh();
        const char *thumb = t->album.covers[0].thumb_url;
        const char *full = t->album.covers[0].url;
        int got = -1;
        ghost_log_init(1, 0);
        if (thumb && *thumb)
            got = termimg_load_url(thumb, &t->cover);
        if (got != 0 && full && *full && (!thumb || strcmp(full, thumb) != 0))
            got = termimg_load_url(full, &t->cover);
        ghost_log_init(q, v);
        (void)got;
    }
    set_status(t, "Enter info  d download  f format  Esc search  q quit");
}

static int do_download(struct Tui *t) {
    if (!t->album_loaded)
        do_lookup(t);
    if (!t->album_loaded)
        return 0;
    if (t->cover_shown)
        termimg_clear();
    t->cover_shown = 0;
    def_prog_mode();
    endwin();

    struct Options o = *t->opt;
    o.format = t->fmt[0] ? t->fmt : t->opt->format;
    o.use_default = 1;
    o.tui = 0;
    int rc = download_album(&t->album, &o, t->abort_flag);
    fprintf(stderr, "\nPress Enter to return to search...\n");
    fflush(stderr);
    int c;
    while ((c = getchar()) != '\n' && c != EOF) {
    }
    reset_prog_mode();
    refresh();
    if (rc == 0)
        set_status(t, "Download complete");
    else
        set_status(t, "Download finished with errors");
    return 0;
}

static void cycle_format(struct Tui *t) {
    if (!t->album_loaded || t->album.nformats <= 0)
        return;
    int idx = 0;
    for (int i = 0; i < t->album.nformats; i++) {
        if (str_eq_ci(t->album.formats[i], t->fmt)) {
            idx = (i + 1) % t->album.nformats;
            break;
        }
    }
    snprintf(t->fmt, sizeof(t->fmt), "%s", t->album.formats[idx]);
    snprintf(t->status, sizeof(t->status), "Format: %s", t->fmt);
}

static void draw(struct Tui *t) {
    erase();
    int w = COLS;
    int h = LINES;
    if (w < 40)
        w = 40;
    if (h < 12)
        h = 12;

    int split = (w >= 100);
    int list_x = 0, list_w, detail_x = 0, detail_w = 0;
    int list_y = 3, list_h;
    int detail_y, detail_h;
    int help_y = h - 2;

    if (split) {
        list_w = w / 2;
        detail_x = list_w;
        detail_w = w - list_w;
        list_h = help_y - list_y;
        detail_y = list_y;
        detail_h = list_h;
    } else {
        list_w = w;
        list_h = (help_y - list_y) / 2;
        detail_y = list_y + list_h;
        detail_h = help_y - detail_y;
        detail_w = w;
        detail_x = 0;
    }

    /* header / search */
    draw_box_title(0, 0, 3, w, " ghost-dl search ");
    mvaddstr(1, 2, "Search: ");
    attron(COLOR_PAIR(3) | A_BOLD);
    mvaddnstr(1, 10, t->query, w - 14);
    attroff(COLOR_PAIR(3) | A_BOLD);
    if (t->focus == FOCUS_SEARCH) {
        mvaddch(1, 10 + t->qlen, ACS_CKBOARD);
    }

    draw_box_title(list_y, list_x, list_h, list_w, " results ");
    int inner_h = list_h - 2;
    int inner_w = list_w - 2;
    if (inner_h < 1)
        inner_h = 1;
    if (t->selected < t->scroll)
        t->scroll = t->selected;
    if (t->selected >= t->scroll + inner_h)
        t->scroll = t->selected - inner_h + 1;
    if (t->scroll < 0)
        t->scroll = 0;

    if (t->results.count == 0) {
        put_trunc(list_y + 2, list_x + 2, inner_w - 2,
                  t->qlen ? "No results." : "Type a query and press Enter.",
                  COLOR_PAIR(4));
        put_trunc(list_y + 4, list_x + 2, inner_w - 2,
                  "Examples: minecraft, celeste, persona 5", COLOR_PAIR(4));
    } else {
        for (int i = 0; i < inner_h; i++) {
            int idx = t->scroll + i;
            if (idx >= t->results.count)
                break;
            struct SearchHit *hit = &t->results.items[idx];
            int y = list_y + 1 + i;
            int sel = (idx == t->selected);
            int attrs = sel ? (COLOR_PAIR(5) | A_REVERSE | A_BOLD) : COLOR_PAIR(3);
            char line[512];
            snprintf(line, sizeof(line), "%s%s%s%s%s",
                     hit->title ? hit->title : hit->id,
                     hit->year && hit->year[0] ? "  " : "",
                     hit->year && hit->year[0] ? hit->year : "",
                     hit->type && hit->type[0] ? "  " : "",
                     hit->type && hit->type[0] ? hit->type : "");
            if (sel) {
                move(y, list_x + 1);
                attron(attrs);
                for (int x = 0; x < inner_w; x++)
                    addch(' ');
                attroff(attrs);
            }
            put_trunc(y, list_x + 2, inner_w - 2, line, attrs);
        }
    }

    draw_box_title(detail_y, detail_x, detail_h, detail_w, " album ");
    int dy = detail_y + 1;
    int dx = detail_x + 2;
    int dw = detail_w - 4;
    int dmax = detail_y + detail_h - 2;
    t->cover_row = t->cover_col = t->cover_cols = t->cover_rows = 0;
    if (t->album_loaded) {
        char line[256];
        put_trunc(dy++, dx, dw, t->album.title ? t->album.title : "", COLOR_PAIR(2) | A_BOLD);
        if (t->cover.rgb && termimg_supported() && dw >= 10 && dy + 8 <= dmax) {
            int cols = dw > 24 ? 18 : (dw > 16 ? 14 : 10);
            if (cols > dw)
                cols = dw;
            int rows = cols / 2;
            if (rows < 6)
                rows = 6;
            if (rows > 10)
                rows = 10;
            if (dy + rows + 5 > dmax)
                rows = dmax - dy - 5;
            if (rows >= 4) {
                t->cover_row = dy;
                t->cover_col = dx;
                t->cover_cols = cols;
                t->cover_rows = rows;
                dy += rows + 1;
            }
        }
        snprintf(line, sizeof(line), "Year: %s    Type: %s",
                 t->album.year ? t->album.year : "-",
                 t->album.type ? t->album.type : "-");
        if (dy <= dmax)
            put_trunc(dy++, dx, dw, line, COLOR_PAIR(3));
        snprintf(line, sizeof(line), "Platforms: %s",
                 t->album.platforms ? t->album.platforms : "-");
        if (dy <= dmax)
            put_trunc(dy++, dx, dw, line, COLOR_PAIR(3));
        snprintf(line, sizeof(line), "Tracks: %d    Size: %s    Covers: %d",
                 t->album.ntracks,
                 t->album.filesize ? t->album.filesize : "-",
                 t->album.ncovers);
        if (dy <= dmax)
            put_trunc(dy++, dx, dw, line, COLOR_PAIR(3));
        snprintf(line, sizeof(line), "Format: %s   (f to cycle)", t->fmt[0] ? t->fmt : "-");
        if (dy <= dmax)
            put_trunc(dy++, dx, dw, line, COLOR_PAIR(6) | A_BOLD);
        if (dy <= dmax)
            dy++;
        if (dy <= dmax)
            put_trunc(dy++, dx, dw, "Tracks:", COLOR_PAIR(2));
        int multi = 0;
        for (int i = 0; i < t->album.ntracks; i++) {
            if (t->album.tracks[i].disc > 1)
                multi = 1;
        }
        for (int i = 0; i < t->album.ntracks && dy <= dmax; i++) {
            struct Track *tr = &t->album.tracks[i];
            if (multi && tr->disc > 0)
                snprintf(line, sizeof(line), "CD%d %02d. %s%s%s",
                         tr->disc, tr->number, tr->title ? tr->title : "",
                         tr->duration ? "  " : "",
                         tr->duration ? tr->duration : "");
            else
                snprintf(line, sizeof(line), "%02d. %s%s%s",
                         tr->number, tr->title ? tr->title : "",
                         tr->duration ? "  " : "",
                         tr->duration ? tr->duration : "");
            put_trunc(dy++, dx, dw, line, COLOR_PAIR(3));
        }
        if (t->album.ntracks > (dmax - (detail_y + 8)) && dmax >= dy - 1) {
            /* truncated */
        }
    } else if (t->results.count > 0) {
        put_trunc(dy + 1, dx, dw, "Press Enter to load album info.", COLOR_PAIR(4));
        put_trunc(dy + 2, dx, dw, "Press d to download the selected album.", COLOR_PAIR(4));
        if (t->selected >= 0 && t->selected < t->results.count) {
            struct SearchHit *hit = &t->results.items[t->selected];
            put_trunc(dy + 4, dx, dw, hit->title ? hit->title : "", COLOR_PAIR(2) | A_BOLD);
            char line[256];
            snprintf(line, sizeof(line), "%s    %s    %s",
                     hit->year ? hit->year : "",
                     hit->type ? hit->type : "",
                     hit->platforms ? hit->platforms : "");
            put_trunc(dy + 5, dx, dw, line, COLOR_PAIR(3));
            snprintf(line, sizeof(line), "id: %s", hit->id ? hit->id : "");
            put_trunc(dy + 6, dx, dw, line, COLOR_PAIR(4));
        }
    } else {
        put_trunc(dy + 1, dx, dw, "KHInsider Game OST Downloader", COLOR_PAIR(2) | A_BOLD);
        put_trunc(dy + 3, dx, dw, "Search the archive, inspect an album,", COLOR_PAIR(3));
        put_trunc(dy + 4, dx, dw, "then download with a live progress bar.", COLOR_PAIR(3));
    }

    /* help / status */
    attron(COLOR_PAIR(1));
    mvhline(help_y, 0, ACS_HLINE, w);
    attroff(COLOR_PAIR(1));
    attron(COLOR_PAIR(4));
    mvaddnstr(help_y, 2, t->status, w - 4);
    attroff(COLOR_PAIR(4));
    attron(COLOR_PAIR(4));
    mvaddnstr(h - 1, 2,
              "Enter search/info   Esc search   d download   f format   q quit   arrows move",
              w - 4);
    attroff(COLOR_PAIR(4));
    refresh();
    if (t->album_loaded && t->cover.rgb && t->cover_rows >= 4) {
        termimg_show(&t->cover, t->cover_row, t->cover_col, t->cover_cols, t->cover_rows);
        t->cover_shown = 1;
    } else if (t->cover_shown) {
        termimg_clear();
        t->cover_shown = 0;
    }
}

static void handle_key(struct Tui *t, int ch) {
    if (ch == KEY_RESIZE) {
        t->cover_shown = 0;
        return;
    }
    if (ch == 3) { /* Ctrl-C */
        t->running = 0;
        return;
    }
    if (t->focus == FOCUS_SEARCH) {
        if (ch == '\n' || ch == KEY_ENTER) {
            do_search(t);
            if (t->results.count > 0)
                t->focus = FOCUS_LIST;
            return;
        }
        if (ch == 27)
            return;
        if (ch == KEY_DOWN && t->results.count > 0) {
            t->focus = FOCUS_LIST;
            return;
        }
        if (ch == KEY_BACKSPACE || ch == 127 || ch == 8) {
            if (t->qlen > 0)
                t->query[--t->qlen] = 0;
            return;
        }
        if (ch == 'q' && t->qlen == 0) {
            t->running = 0;
            return;
        }
        if (ch >= 32 && ch < 127 && t->qlen < (int)sizeof(t->query) - 1) {
            t->query[t->qlen++] = (char)ch;
            t->query[t->qlen] = 0;
        }
        return;
    }

    switch (ch) {
    case 27: /* Escape: return to the search bar */
#ifdef KEY_EXIT
    case KEY_EXIT:
#endif
#ifdef KEY_CANCEL
    case KEY_CANCEL:
#endif
        t->focus = FOCUS_SEARCH;
        set_status(t, "Type a search and press Enter");
        break;
    case 'q':
    case 'Q':
        t->running = 0;
        break;
    case '/':
    case 's':
    case 'S':
        t->focus = FOCUS_SEARCH;
        break;
    case KEY_UP:
    case 'k':
        if (t->selected > 0) {
            t->selected--;
            if (t->album_sel != t->selected)
                album_clear(t);
        }
        break;
    case KEY_DOWN:
    case 'j':
        if (t->selected + 1 < t->results.count) {
            t->selected++;
            if (t->album_sel != t->selected)
                album_clear(t);
        }
        break;
    case KEY_PPAGE:
        t->selected -= 10;
        if (t->selected < 0)
            t->selected = 0;
        album_clear(t);
        break;
    case KEY_NPAGE:
        t->selected += 10;
        if (t->selected >= t->results.count)
            t->selected = t->results.count ? t->results.count - 1 : 0;
        album_clear(t);
        break;
    case '\n':
    case KEY_ENTER:
    case 'i':
    case 'I':
        do_lookup(t);
        break;
    case 'd':
    case 'D':
        do_download(t);
        break;
    case 'f':
    case 'F':
        if (!t->album_loaded)
            do_lookup(t);
        cycle_format(t);
        break;
    default:
        break;
    }
}

int tui_run(struct Options *opt, volatile int *abort_flag) {
    setlocale(LC_ALL, "");
    struct Tui t;
    memset(&t, 0, sizeof(t));
    t.opt = opt;
    t.abort_flag = abort_flag;
    t.album_sel = -1;
    t.focus = FOCUS_SEARCH;
    t.running = 1;
    if (opt->search && *opt->search) {
        snprintf(t.query, sizeof(t.query), "%s", opt->search);
        t.qlen = (int)strlen(t.query);
    }
    snprintf(t.status, sizeof(t.status), "Type a search and press Enter");

    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    set_escdelay(25);
    curs_set(0);
    if (has_colors()) {
        start_color();
        use_default_colors();
        init_pair(1, COLOR_CYAN, -1);     /* boxes */
        init_pair(2, COLOR_MAGENTA, -1);  /* titles */
        init_pair(3, COLOR_WHITE, -1);    /* body */
        init_pair(4, COLOR_YELLOW, -1);   /* muted / log */
        init_pair(5, COLOR_BLACK, COLOR_CYAN);
        init_pair(6, COLOR_GREEN, -1);
    }

    if (t.qlen > 0)
        do_search(&t);

    while (t.running && !(abort_flag && *abort_flag)) {
        draw(&t);
        int ch = getch();
        handle_key(&t, ch);
    }

    if (t.cover_shown)
        termimg_clear();
    t.cover_shown = 0;
    endwin();
    album_clear(&t);
    search_free(&t.results);
    return 0;
}

#else /* !HAVE_NCURSES */

int tui_run(struct Options *opt, volatile int *abort_flag) {
    (void)abort_flag;
    if (opt->search && *opt->search) {
        struct SearchResults sr;
        if (fetch_search(opt->search, &sr) != 0)
            return GHOST_ERR_NETWORK;
        if (sr.count == 0) {
            fprintf(stdout, "No albums found for \"%s\".\n", opt->search);
            search_free(&sr);
            return 0;
        }
        fprintf(stdout, "Found %d album%s for \"%s\":\n\n",
                sr.count, sr.count == 1 ? "" : "s", opt->search);
        for (int i = 0; i < sr.count; i++) {
            struct SearchHit *h = &sr.items[i];
            fprintf(stdout, "  %-40s  %s  %s\n    %s\n",
                    h->title ? h->title : "",
                    h->year ? h->year : "",
                    h->type ? h->type : "",
                    h->id ? h->id : "");
        }
        search_free(&sr);
        return 0;
    }
    log_error("This build has no TUI (ncurses was not found).");
    log_info("Use: ghost-dl --search QUERY    or pass an album URL/slug");
    return GHOST_ERR_USAGE;
}

#endif
