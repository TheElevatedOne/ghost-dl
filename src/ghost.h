#ifndef GHOST_H
#define GHOST_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#ifndef GHOST_VERSION
#define GHOST_VERSION "2.1.0"
#endif

#define GHOST_BASE_URL "https://downloads.khinsider.com"
#define GHOST_UA \
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 " \
    "(KHTML, like Gecko) Chrome/139.0.0.0 Safari/537.36"

enum {
    GHOST_OK = 0,
    GHOST_ERR = 1,
    GHOST_ERR_USAGE = 2,
    GHOST_ERR_NETWORK = 3,
    GHOST_ERR_PARSE = 4,
    GHOST_ERR_IO = 5
};

struct Options {
    char **inputs;
    int ninputs;
    char *output;
    char *format;
    char *search;
    int threads;
    int use_default;
    int quiet;
    int verbose;
    int info_only;
    int dry_run;
    int no_cover;
    int cover_only;
    int force;
    int tui;
};

struct Cover {
    char *url;
    char *thumb_url;
};

struct Track {
    int disc;
    int number;
    char *title;
    char *page_url;
    char *download_url;
    char *ext;
    char *duration;
    char *size_str;
};

struct Album {
    char *id;
    char *url;
    char *title;
    char *year;
    char *type;
    char *platforms;
    char *publisher;
    char *filesize;
    char *date_added;
    char *description;
    int nfiles;
    char **formats;
    int nformats;
    struct Track *tracks;
    int ntracks;
    struct Cover *covers;
    int ncovers;
};

struct SearchHit {
    char *id;
    char *title;
    char *platforms;
    char *type;
    char *year;
};

struct SearchResults {
    struct SearchHit *items;
    int count;
};

/* logging / tty */
void ghost_log_init(int quiet, int verbose);
int ghost_log_quiet(void);
int ghost_log_verbose(void);
int ghost_is_tty(FILE *fp);
int ghost_color_enabled(void);
void log_info(const char *fmt, ...);
void log_error(const char *fmt, ...);
void log_verbose(const char *fmt, ...);
void log_input(const char *fmt, ...);
void log_final(const char *fmt, ...);
void log_dl(const char *fmt, ...);

/* util */
void *xmalloc(size_t n);
void *xcalloc(size_t n, size_t sz);
void *xrealloc(void *p, size_t n);
char *xstrdup(const char *s);
char *xstrndup(const char *s, size_t n);
char *str_trim(char *s);
char *str_lower_dup(const char *s);
int str_eq_ci(const char *a, const char *b);
int str_ends_ci(const char *s, const char *suf);
int cpu_threads(void);
int mkdir_p(const char *path);
int path_is_dir(const char *path);
int path_is_file(const char *path);
int path_exists(const char *path);
char *path_join(const char *a, const char *b);
char *filename_sanitize(const char *name);
char *url_decode(const char *s);
char *url_encode(const char *s);
char *url_join(const char *base, const char *href);
char *html_unescape(const char *s);
char *html_text(const char *s, size_t n);
char *html_extract(const char *s, const char *start, const char *end);
const char *html_find(const char *s, const char *needle);
char *album_folder_name(const struct Album *al);
char *track_filename(const struct Track *tr, int multi_disc, int num_width);
int format_rank(const char *fmt);
int is_audio_format(const char *fmt);
const char *pick_format(char **available, int n, const char *preference, int use_default);
int default_thread_count(void);
char *slug_from_input(const char *in);
int looks_like_url(const char *s);
int looks_like_khinsider(const char *s);
char *read_file_text(const char *path, size_t *len);
int64_t monotonic_ms(void);
void human_bytes(char *buf, size_t buflen, int64_t n);
void human_rate(char *buf, size_t buflen, double bps);
void human_duration(char *buf, size_t buflen, double seconds);

void album_free(struct Album *al);
void search_free(struct SearchResults *sr);
void options_free(struct Options *opt);

/* http */
int http_global_init(void);
void http_global_cleanup(void);
int http_get(const char *url, char **body, size_t *len, char **final_url);
int http_download(const char *url, const char *path,
                  int (*progress)(void *ud, int64_t total, int64_t now),
                  void *ud, int64_t *out_total, volatile int *abort_flag);

/* parse */
int parse_album_page(const char *html, const char *page_url, struct Album *out);
int parse_search_page(const char *html, const char *final_url, struct SearchResults *out);
int parse_song_page(const char *html, const char *want_ext, char **download_url);
int fetch_album(const char *input, struct Album *out);
int fetch_search(const char *query, struct SearchResults *out);
int resolve_track_url(struct Track *tr, const char *fmt);
int resolve_album_tracks(struct Album *al, const char *fmt, int threads, volatile int *abort_flag);

/* download */
int download_album(struct Album *al, const struct Options *opt, volatile int *abort_flag);
int prompt_format(const struct Album *al, const struct Options *opt, char *out, size_t outsz);
void print_album_info(const struct Album *al, FILE *fp);

/* terminal image preview (Kitty graphics protocol) */
struct TermImg {
    unsigned char *rgb;
    int w;
    int h;
};

int termimg_supported(void);
void termimg_clear(void);
int termimg_load_url(const char *url, struct TermImg *img);
void termimg_free(struct TermImg *img);
int termimg_show(const struct TermImg *img, int row, int col, int cols, int rows);

/* tui */
int tui_run(struct Options *opt, volatile int *abort_flag);

/* cli */
void print_help(void);
void print_version(void);
int parse_args(int argc, char **argv, struct Options *opt);
int run_cli(struct Options *opt, volatile int *abort_flag);

#endif
