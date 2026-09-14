#include "ghost.h"

#include <getopt.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static volatile int g_abort = 0;

static void on_sigint(int sig) {
    (void)sig;
    g_abort = 1;
}

void print_version(void) {
    printf("ghost-dl %s\n", GHOST_VERSION);
}

void print_help(void) {
    printf(
        "Usage: ghost-dl [OPTIONS] [INPUT...]\n"
        "\n"
        "Downloader for Kingdom Hearts Insider game OSTs\n"
        "(https://downloads.khinsider.com).\n"
        "\n"
        "INPUT may be an album URL, an album slug, or a batch file of URLs/slugs\n"
        "(one per line). With no INPUT, the search TUI is opened.\n"
        "\n"
        "Examples:\n"
        "  ghost-dl\n"
        "  ghost-dl --search celeste\n"
        "  ghost-dl -d -f flac minecraft\n"
        "  ghost-dl -o ~/Music https://downloads.khinsider.com/game-soundtracks/album/minecraft\n"
        "  ghost-dl --info persona-5\n"
        "  ghost-dl -d urls.txt\n"
        "\n"
        "General:\n"
        "  -h, --help              Show this help and exit\n"
        "  -V, --version           Show version and exit\n"
        "      --tui               Open the search TUI\n"
        "  -s, --search QUERY      Search albums and print results\n"
        "  -i, --info              Show album info without downloading\n"
        "  -n, --dry-run           Resolve files but do not download\n"
        "\n"
        "Download:\n"
        "  -o, --output DIR        Output directory (default: current directory)\n"
        "  -t, --threads N         Parallel workers (default: CPU/2, max 8)\n"
        "  -f, --format FMT        Audio format, or comma-separated preference\n"
        "                          (e.g. flac,mp3). Default: highest available\n"
        "  -d, --default           Highest quality without prompting\n"
        "  -y, --yes               Same as --default\n"
        "      --no-cover          Skip album art\n"
        "      --cover-only        Download only album art\n"
        "      --force             Overwrite existing files\n"
        "\n"
        "Logging:\n"
        "  -q, --quiet             Suppress progress and log messages\n"
        "  -v, --verbose           Extra log messages\n"
        "\n"
        "TUI keys:  type to search, Enter lookup, Esc search, d download, f format, q quit\n"
    );
}

static struct option long_opts[] = {
    {"help", no_argument, NULL, 'h'},
    {"version", no_argument, NULL, 'V'},
    {"output", required_argument, NULL, 'o'},
    {"threads", required_argument, NULL, 't'},
    {"format", required_argument, NULL, 'f'},
    {"default", no_argument, NULL, 'd'},
    {"yes", no_argument, NULL, 'y'},
    {"quiet", no_argument, NULL, 'q'},
    {"verbose", no_argument, NULL, 'v'},
    {"search", required_argument, NULL, 's'},
    {"info", no_argument, NULL, 'i'},
    {"dry-run", no_argument, NULL, 'n'},
    {"tui", no_argument, NULL, 1000},
    {"no-cover", no_argument, NULL, 1001},
    {"cover-only", no_argument, NULL, 1002},
    {"force", no_argument, NULL, 1003},
    {0, 0, 0, 0},
};

int parse_args(int argc, char **argv, struct Options *opt) {
    memset(opt, 0, sizeof(*opt));
    opt->threads = default_thread_count();

    opterr = 0;
    int c;
    while ((c = getopt_long(argc, argv, ":hVo:t:f:dyqvs:in", long_opts, NULL)) != -1) {
        switch (c) {
        case 'h':
            print_help();
            exit(0);
        case 'V':
            print_version();
            exit(0);
        case 'o':
            free(opt->output);
            opt->output = xstrdup(optarg);
            break;
        case 't': {
            int n = atoi(optarg);
            if (n < 1)
                n = 1;
            if (n > 32)
                n = 32;
            opt->threads = n;
            break;
        }
        case 'f':
            free(opt->format);
            opt->format = xstrdup(optarg);
            break;
        case 'd':
        case 'y':
            opt->use_default = 1;
            break;
        case 'q':
            opt->quiet = 1;
            break;
        case 'v':
            opt->verbose = 1;
            break;
        case 's':
            free(opt->search);
            opt->search = xstrdup(optarg);
            break;
        case 'i':
            opt->info_only = 1;
            break;
        case 'n':
            opt->dry_run = 1;
            break;
        case 1000:
            opt->tui = 1;
            break;
        case 1001:
            opt->no_cover = 1;
            break;
        case 1002:
            opt->cover_only = 1;
            break;
        case 1003:
            opt->force = 1;
            break;
        case ':':
            fprintf(stderr, "ghost-dl: option -%c requires an argument\n", optopt);
            return -1;
        case '?':
        default:
            fprintf(stderr, "ghost-dl: unknown option");
            if (optopt)
                fprintf(stderr, " -%c", optopt);
            fprintf(stderr, "\nTry 'ghost-dl --help'.\n");
            return -1;
        }
    }

    for (int i = optind; i < argc; i++) {
        opt->inputs = xrealloc(opt->inputs, (size_t)(opt->ninputs + 1) * sizeof(char *));
        opt->inputs[opt->ninputs++] = xstrdup(argv[i]);
    }
    return 0;
}

static int load_batch_file(const char *path, char ***out, int *nout) {
    char *text = read_file_text(path, NULL);
    if (!text)
        return -1;
    int n = 0, cap = 0;
    char **items = NULL;
    char *save = NULL;
    for (char *line = strtok_r(text, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        char *t = str_trim(line);
        if (!*t || *t == '#')
            continue;
        /* strip CR */
        size_t L = strlen(t);
        if (L && t[L - 1] == '\r')
            t[L - 1] = 0;
        if (n + 1 > cap) {
            cap = cap ? cap * 2 : 8;
            items = xrealloc(items, (size_t)cap * sizeof(char *));
        }
        items[n++] = xstrdup(t);
    }
    free(text);
    *out = items;
    *nout = n;
    return 0;
}

static int expand_inputs(struct Options *opt, char ***jobs, int *njobs) {
    char **out = NULL;
    int n = 0, cap = 0;
    for (int i = 0; i < opt->ninputs; i++) {
        const char *in = opt->inputs[i];
        if (path_is_file(in)) {
            char **batch = NULL;
            int nb = 0;
            if (load_batch_file(in, &batch, &nb) != 0) {
                log_error("Could not read batch file: %s", in);
                for (int k = 0; k < n; k++)
                    free(out[k]);
                free(out);
                return -1;
            }
            for (int j = 0; j < nb; j++) {
                if (n + 1 > cap) {
                    cap = cap ? cap * 2 : 8;
                    out = xrealloc(out, (size_t)cap * sizeof(char *));
                }
                out[n++] = batch[j];
            }
            free(batch);
        } else {
            if (n + 1 > cap) {
                cap = cap ? cap * 2 : 8;
                out = xrealloc(out, (size_t)cap * sizeof(char *));
            }
            out[n++] = xstrdup(in);
        }
    }
    *jobs = out;
    *njobs = n;
    return 0;
}

static int print_search(const char *query) {
    struct SearchResults sr;
    if (fetch_search(query, &sr) != 0)
        return GHOST_ERR_NETWORK;
    if (sr.count == 0) {
        printf("No albums found for \"%s\".\n", query);
        search_free(&sr);
        return 0;
    }
    printf("Found %d album%s for \"%s\":\n\n", sr.count, sr.count == 1 ? "" : "s", query);
    int idw = 8;
    for (int i = 0; i < sr.count; i++) {
        int L = sr.items[i].id ? (int)strlen(sr.items[i].id) : 0;
        if (L > idw)
            idw = L;
    }
    if (idw > 56)
        idw = 56;
    printf("  %-*s  %-6s %-14s %s\n", idw, "ID", "YEAR", "TYPE", "TITLE");
    for (int i = 0; i < sr.count; i++) {
        struct SearchHit *h = &sr.items[i];
        printf("  %-*s  %-6s %-14s %s\n",
               idw,
               h->id ? h->id : "",
               h->year ? h->year : "",
               h->type ? h->type : "",
               h->title ? h->title : "");
    }
    printf("\nDownload with:  ghost-dl -d -f flac <ID>\n");
    search_free(&sr);
    return 0;
}

int run_cli(struct Options *opt, volatile int *abort_flag) {
    char **jobs = NULL;
    int njobs = 0;
    if (expand_inputs(opt, &jobs, &njobs) != 0)
        return GHOST_ERR_IO;
    if (njobs == 0) {
        log_error("No album URLs, slugs, or batch entries to process");
        return GHOST_ERR_USAGE;
    }

    int rc = 0;
    for (int i = 0; i < njobs; i++) {
        if (abort_flag && *abort_flag)
            break;
        if (njobs > 1)
            log_info("Job #%02d started", i);
        struct Album al;
        if (fetch_album(jobs[i], &al) != 0) {
            rc = GHOST_ERR_PARSE;
            continue;
        }
        if (opt->info_only && !opt->dry_run) {
            print_album_info(&al, stdout);
            album_free(&al);
            continue;
        }
        int drc = download_album(&al, opt, abort_flag);
        album_free(&al);
        if (drc != 0)
            rc = GHOST_ERR;
        if (njobs > 1)
            log_info("Job #%02d finished", i);
    }
    for (int i = 0; i < njobs; i++)
        free(jobs[i]);
    free(jobs);
    return rc;
}

int main(int argc, char **argv) {
    struct Options opt;
    if (parse_args(argc, argv, &opt) != 0)
        return GHOST_ERR_USAGE;

    if (opt.quiet && opt.verbose) {
        fprintf(stderr, "ghost-dl: --quiet and --verbose cannot be used together\n");
        options_free(&opt);
        return GHOST_ERR_USAGE;
    }
    ghost_log_init(opt.quiet, opt.verbose);

    if (http_global_init() != 0) {
        fprintf(stderr, "ghost-dl: failed to initialise libcurl\n");
        options_free(&opt);
        return GHOST_ERR_NETWORK;
    }

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_sigint;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    int want_tui = opt.tui || (opt.ninputs == 0 && !opt.search);
    int rc = 0;

    if (opt.ninputs > 0) {
        rc = run_cli(&opt, &g_abort);
    } else if (want_tui) {
        if (!ghost_is_tty(stdin) || !ghost_is_tty(stdout)) {
            fprintf(stderr, "ghost-dl: no INPUT given and stdin is not a TTY.\n"
                            "Pass an album URL/slug, a batch file, or --search QUERY.\n");
            rc = GHOST_ERR_USAGE;
        } else {
            rc = tui_run(&opt, &g_abort);
        }
    } else if (opt.search) {
        rc = print_search(opt.search);
    }

    if (g_abort) {
        fprintf(stderr, "\n\033[38;2;229;80;26m[INTERRUPT]\033[0m Process stopped\n");
        rc = 130;
    }

    http_global_cleanup();
    options_free(&opt);
    return rc;
}
