#include "ghost.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_NO_HDR
#define STBI_NO_PIC
#define STBI_NO_PNM
#define STBI_NO_PSD
#define STBI_NO_TGA
#define STBI_NO_BMP
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_ONLY_GIF
#define STBI_NO_FAILURE_STRINGS
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#endif
#include "stb_image.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#define TERMIMG_ID 99
#define TERMIMG_MAX_EDGE 320

int termimg_supported(void) {
    if (!isatty(STDOUT_FILENO))
        return 0;
    /* Graphics protocol is unreliable inside tmux/screen. */
    if (getenv("TMUX") || getenv("STY"))
        return 0;
    if (getenv("KITTY_WINDOW_ID"))
        return 1;
    if (getenv("WEZTERM_EXECUTABLE") || getenv("WEZTERM_PANE"))
        return 1;
    if (getenv("GHOSTTY_RESOURCES_DIR") || getenv("GHOSTTY_BIN"))
        return 1;
    const char *tp = getenv("TERM_PROGRAM");
    if (tp && (str_eq_ci(tp, "WezTerm") || str_eq_ci(tp, "ghostty") ||
               str_eq_ci(tp, "kitty")))
        return 1;
    const char *term = getenv("TERM");
    if (term && (strstr(term, "kitty") || strstr(term, "ghostty") ||
                 strstr(term, "wezterm")))
        return 1;
    return 0;
}

void termimg_clear(void) {
    if (!termimg_supported())
        return;
    fputs("\033_Ga=d,d=i,i=99,q=2\033\\", stdout);
    fflush(stdout);
}

void termimg_free(struct TermImg *img) {
    if (!img)
        return;
    free(img->rgb);
    img->rgb = NULL;
    img->w = img->h = 0;
}

static unsigned char *scale_rgb(const unsigned char *src, int sw, int sh,
                                int dw, int dh) {
    unsigned char *dst = xmalloc((size_t)dw * (size_t)dh * 3);
    for (int y = 0; y < dh; y++) {
        int sy = y * sh / dh;
        if (sy >= sh)
            sy = sh - 1;
        for (int x = 0; x < dw; x++) {
            int sx = x * sw / dw;
            if (sx >= sw)
                sx = sw - 1;
            memcpy(dst + ((size_t)y * dw + x) * 3,
                   src + ((size_t)sy * sw + sx) * 3, 3);
        }
    }
    return dst;
}

int termimg_load_url(const char *url, struct TermImg *img) {
    memset(img, 0, sizeof(*img));
    if (!url || !*url)
        return -1;
    char *body = NULL;
    size_t len = 0;
    if (http_get(url, &body, &len, NULL) != 0 || !body || len < 16) {
        free(body);
        return -1;
    }
    int w = 0, h = 0, n = 0;
    unsigned char *rgb = stbi_load_from_memory((unsigned char *)body, (int)len,
                                               &w, &h, &n, 3);
    free(body);
    if (!rgb || w <= 0 || h <= 0) {
        stbi_image_free(rgb);
        return -1;
    }
    int dw = w, dh = h;
    if (dw > TERMIMG_MAX_EDGE || dh > TERMIMG_MAX_EDGE) {
        if (dw >= dh) {
            dh = dh * TERMIMG_MAX_EDGE / dw;
            dw = TERMIMG_MAX_EDGE;
        } else {
            dw = dw * TERMIMG_MAX_EDGE / dh;
            dh = TERMIMG_MAX_EDGE;
        }
        if (dw < 1)
            dw = 1;
        if (dh < 1)
            dh = 1;
        unsigned char *scaled = scale_rgb(rgb, w, h, dw, dh);
        stbi_image_free(rgb);
        rgb = scaled;
        w = dw;
        h = dh;
    }
    img->rgb = rgb;
    img->w = w;
    img->h = h;
    return 0;
}

static const char b64tab[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static size_t b64_encode(const unsigned char *in, size_t n, char *out) {
    size_t j = 0;
    for (size_t i = 0; i < n; i += 3) {
        unsigned v = (unsigned)in[i] << 16;
        if (i + 1 < n)
            v |= (unsigned)in[i + 1] << 8;
        if (i + 2 < n)
            v |= (unsigned)in[i + 2];
        out[j++] = b64tab[(v >> 18) & 63];
        out[j++] = b64tab[(v >> 12) & 63];
        out[j++] = (i + 1 < n) ? b64tab[(v >> 6) & 63] : '=';
        out[j++] = (i + 2 < n) ? b64tab[v & 63] : '=';
    }
    out[j] = 0;
    return j;
}

int termimg_show(const struct TermImg *img, int row, int col, int cols, int rows) {
    if (!img || !img->rgb || img->w <= 0 || img->h <= 0)
        return -1;
    if (!termimg_supported())
        return -1;
    if (cols < 2 || rows < 2)
        return -1;
    if (row < 0)
        row = 0;
    if (col < 0)
        col = 0;

    size_t nbytes = (size_t)img->w * (size_t)img->h * 3;
    char *b64 = xmalloc((nbytes / 3 + 2) * 4 + 8);
    size_t blen = b64_encode(img->rgb, nbytes, b64);

    /* 1-based cursor. C=1: do not move cursor. q=2: no replies. */
    fprintf(stdout, "\033[%d;%dH", row + 1, col + 1);

    const size_t chunk = 4096;
    size_t off = 0;
    int first = 1;
    while (off < blen) {
        size_t n = blen - off;
        if (n > chunk)
            n = chunk;
        int more = (off + n < blen);
        if (first) {
            fprintf(stdout,
                    "\033_Ga=T,f=24,s=%d,h=%d,c=%d,r=%d,C=1,i=%d,q=2,m=%d;",
                    img->w, img->h, cols, rows, TERMIMG_ID, more ? 1 : 0);
            first = 0;
        } else {
            fprintf(stdout, "\033_Gm=%d;", more ? 1 : 0);
        }
        fwrite(b64 + off, 1, n, stdout);
        fputs("\033\\", stdout);
        off += n;
    }
    free(b64);
    fflush(stdout);
    return 0;
}
