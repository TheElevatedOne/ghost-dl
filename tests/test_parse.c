#include "ghost.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_fail;

static void expect(int cond, const char *msg) {
    if (!cond) {
        fprintf(stderr, "FAIL: %s\n", msg);
        g_fail++;
    } else {
        fprintf(stdout, "ok   %s\n", msg);
    }
}

static char *load_fix(const char *rel) {
    const char *roots[] = {
        "tests/fixtures",
        "fixtures",
        "../tests/fixtures",
        NULL
    };
    for (int i = 0; roots[i]; i++) {
        char path[512];
        snprintf(path, sizeof(path), "%s/%s", roots[i], rel);
        char *t = read_file_text(path, NULL);
        if (t)
            return t;
    }
    fprintf(stderr, "could not load fixture %s\n", rel);
    exit(2);
}

int main(void) {
    ghost_log_init(1, 0);

    /* slug / url */
    char *s = slug_from_input("https://downloads.khinsider.com/game-soundtracks/album/minecraft");
    expect(s && strcmp(s, "minecraft") == 0, "slug from url");
    free(s);
    s = slug_from_input("https://downloads.khinsider.com/game-soundtracks/album/minecraft/");
    expect(s && strcmp(s, "minecraft") == 0, "slug from url trailing slash");
    free(s);
    s = slug_from_input("minecraft");
    expect(s && strcmp(s, "minecraft") == 0, "slug from slug");
    free(s);
    s = slug_from_input("https://example.com/foo");
    expect(s == NULL, "reject non-khinsider url as slug");
    free(s);

    /* sanitize */
    s = filename_sanitize("a/b:c*?");
    expect(s && strchr(s, '/') == NULL && strchr(s, ':') == NULL, "sanitize reserved chars");
    free(s);

    /* unescape */
    s = html_unescape("A &amp; B &#233; &quot;x&quot;");
    expect(s && strstr(s, "A & B") && strstr(s, "\"x\""), "html unescape");
    free(s);

    /* url decode */
    s = url_decode("1-01.%20Key.mp3");
    expect(s && strcmp(s, "1-01. Key.mp3") == 0, "url decode");
    free(s);

    /* format pick */
    char *fmts[] = {"mp3", "flac"};
    expect(strcmp(pick_format(fmts, 2, NULL, 1), "flac") == 0, "default highest quality is flac");
    expect(strcmp(pick_format(fmts, 2, "ogg,mp3", 0), "mp3") == 0, "preference list picks mp3");
    expect(strcmp(pick_format(fmts, 2, "flac", 0), "flac") == 0, "explicit flac");

    expect(strcmp(pick_format(fmts, 2, "ogg", 0), "flac") == 0,
           "missing preference falls back to best");
    expect(match_format(fmts, 2, "ogg") == NULL, "match_format reports missing format");
    expect(!is_audio_format("cd") && !is_audio_format("time") && is_audio_format("FLAC"),
           "only real audio formats are accepted");

    /* track selection */
    unsigned char sel[10];
    expect(parse_track_selection("1-3,5,9-", 10, sel) == 0 &&
           sel[0] && sel[1] && sel[2] && !sel[3] && sel[4] && !sel[7] && sel[8] && sel[9],
           "track selection ranges");
    expect(parse_track_selection("-2", 10, sel) == 0 && sel[0] && sel[1] && !sel[2],
           "track selection open start");
    expect(parse_track_selection("4-20", 10, sel) == 0 && sel[9], "track selection clamps end");
    expect(parse_track_selection("0", 10, sel) != 0, "track selection rejects 0");
    expect(parse_track_selection("11", 10, sel) != 0, "track selection rejects out of range");
    expect(parse_track_selection("3-1", 10, sel) != 0, "track selection rejects reversed range");
    expect(parse_track_selection("a", 10, sel) != 0, "track selection rejects garbage");

    /* url join */
    s = url_join("https://downloads.khinsider.com/game-soundtracks/album/x", "/foo/bar");
    expect(s && strcmp(s, "https://downloads.khinsider.com/foo/bar") == 0, "url join absolute path");
    free(s);
    s = url_join("https://downloads.khinsider.com/game-soundtracks/album/x", "y.mp3");
    expect(s && strcmp(s, "https://downloads.khinsider.com/game-soundtracks/album/y.mp3") == 0,
           "url join relative path");
    free(s);
    s = url_join("https://a.com/b", "//cdn.com/c");
    expect(s && strcmp(s, "https://cdn.com/c") == 0, "url join protocol-relative");
    free(s);

    /* long names stay below NAME_MAX */
    char longname[600];
    memset(longname, 'a', sizeof(longname) - 1);
    longname[sizeof(longname) - 1] = 0;
    s = filename_sanitize(longname);
    expect(s && strlen(s) <= 200, "long filename is truncated");
    free(s);
    s = html_text("<a>Android</a> , <a>iOS</a>", (size_t)-1);
    expect(s && strcmp(s, "Android, iOS") == 0, "html text list spacing");
    free(s);

    /* minecraft album */
    char *html = load_fix("album_minecraft.html");
    struct Album al;
    int rc = parse_album_page(html,
        "https://downloads.khinsider.com/game-soundtracks/album/minecraft", &al);
    expect(rc == 0, "parse minecraft album");
    expect(al.title && strstr(al.title, "Minecraft") != NULL, "minecraft title");
    expect(al.year && strcmp(al.year, "2011") == 0, "minecraft year");
    expect(al.type && strstr(al.type, "Soundtrack") != NULL, "minecraft type");
    expect(al.ntracks == 54, "minecraft 54 tracks");
    expect(al.nformats >= 2, "minecraft has 2+ formats");
    int has_mp3 = 0, has_flac = 0;
    for (int i = 0; i < al.nformats; i++) {
        if (str_eq_ci(al.formats[i], "mp3"))
            has_mp3 = 1;
        if (str_eq_ci(al.formats[i], "flac"))
            has_flac = 1;
    }
    expect(has_mp3 && has_flac, "minecraft formats mp3+flac");
    expect(al.nformats == 2, "minecraft has exactly mp3+flac (no CD column)");
    expect(al.platforms && !strstr(al.platforms, " ,"), "platforms have no space before comma");
    expect(al.ncovers == 3, "minecraft 3 covers");
    expect(al.covers[0].thumb_url && strstr(al.covers[0].thumb_url, "thumbs") != NULL,
           "minecraft cover has thumbnail url");
    expect(al.tracks[0].title && strcmp(al.tracks[0].title, "Key") == 0, "first track Key");
    expect(al.tracks[0].disc == 1 && al.tracks[0].number == 1, "first track CD1 #1");
    expect(al.tracks[0].page_url && strstr(al.tracks[0].page_url, "Key") != NULL,
           "first track page url");
    expect(al.tracks[24].disc == 2, "track 25 is disc 2");
    expect(al.platforms && strstr(al.platforms, "Windows") != NULL, "platforms include Windows");
    album_free(&al);
    free(html);

    /* wii album: no CD column */
    html = load_fix("album_wii.html");
    rc = parse_album_page(html,
        "https://downloads.khinsider.com/game-soundtracks/album/wii-music-collection", &al);
    expect(rc == 0, "parse wii album");
    expect(al.title && strstr(al.title, "Wii Music") != NULL, "wii title");
    expect(al.year && strcmp(al.year, "2006") == 0, "wii year");
    expect(al.type && strstr(al.type, "Compilation") != NULL, "wii compilation type");
    expect(al.ntracks > 5, "wii has tracks");
    expect(al.tracks[0].number == 1, "wii first track number 1");
    expect(al.tracks[0].disc == 0, "wii has no disc column");
    expect(al.tracks[0].title && strstr(al.tracks[0].title, "Main Menu") != NULL, "wii first title");
    album_free(&al);
    free(html);

    /* search */
    html = load_fix("search_celeste.html");
    struct SearchResults sr;
    rc = parse_search_page(html, "https://downloads.khinsider.com/search?search=celeste", &sr);
    expect(rc == 0, "parse celeste search");
    expect(sr.count >= 5, "celeste search has several hits");
    int found = 0;
    for (int i = 0; i < sr.count; i++) {
        if (sr.items[i].title && strstr(sr.items[i].title, "Celeste"))
            found = 1;
        expect(sr.items[i].id && sr.items[i].id[0], "search hit has id");
    }
    expect(found, "search results include Celeste in title");
    search_free(&sr);
    free(html);

    /* song page */
    html = load_fix("song_key.html");
    char *url = NULL;
    char *ext = NULL;
    rc = parse_song_page(html, "mp3", &url, &ext);
    expect(ext && strcmp(ext, "mp3") == 0, "song ext mp3");
    free(ext);
    expect(rc == 0 && url && strstr(url, ".mp3") && strstr(url, "http"), "song mp3 url");
    expect(url && strstr(url, "vgmtreasurechest") != NULL, "song url is on vgmtreasurechest");
    free(url);
    url = NULL;
    rc = parse_song_page(html, "flac", &url, NULL);
    expect(rc == 0 && url && str_ends_ci(url, ".flac"), "song flac url");
    free(url);
    free(html);

    /* folder name */
    struct Album dummy;
    memset(&dummy, 0, sizeof(dummy));
    dummy.year = xstrdup("2011");
    dummy.title = xstrdup("Minecraft");
    dummy.type = xstrdup("Soundtrack");
    s = album_folder_name(&dummy);
    expect(s && strstr(s, "2011") && strstr(s, "Minecraft") && strstr(s, "Soundtrack"),
           "folder name year-title-type");
    free(s);
    free(dummy.year);
    free(dummy.title);
    free(dummy.type);

    if (g_fail) {
        fprintf(stderr, "\n%d test(s) failed\n", g_fail);
        return 1;
    }
    fprintf(stdout, "\nall tests passed\n");
    return 0;
}
