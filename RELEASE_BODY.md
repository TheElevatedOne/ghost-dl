# Changelog

## 2.2.0

### Fixes

- **Album pages load again.** KHInsider's Cloudflare protection rejected ghost-dl's requests with HTTP 403, so `--info`, downloads, and the TUI album view all failed. Requests now look like a regular browser, and blocked or rate-limited requests (403/429/5xx) are retried with backoff
- The `CD` column header was listed as an audio format (`cd, mp3, flac`)
- The progress display corrupted itself when lines were wider than the terminal; the ETA was also miscalculated
- `-f FMT` with a format the album does not offer now says so and falls back to the best format, instead of silently picking the lowest quality
- TUI: `--tui -s QUERY` focuses the results, so keys like `d` work right away; status messages no longer leave leftover text; UTF-8 search input (e.g. `pokémon`); returning from a download repaints cleanly; Ctrl-C during a TUI download cancels the download instead of quitting
- Better error messages: unknown long options are named, `--threads` is validated, and HTTP failures say what happened (not found, blocked, network error)
- AUR `ghost-dl-bin` pointed to a `v`-prefixed tag that does not exist

### New

- **Resumable downloads.** Interrupted files continue from their `.part` file on the next run
- `-T, --tracks LIST` downloads only some tracks (`1-5,8,12-`); `--info` now prints the track positions
- `--playlist` writes `playlist.m3u8`
- Per-track format fallback when a track is missing in the chosen format
- Track URLs resolve faster thanks to connection reuse, with a live `Resolving track URLs n/N` counter
- Plain per-file progress lines when stderr is not a terminal (logs, CI)
- Download summary (files, bytes, time, average speed)
- Ctrl-C once stops cleanly; twice quits immediately
- Stalled connections time out after 30 s and are retried

### Packages

Debian `.deb`, RPM `.rpm`, pacman `.pkg.tar.zst`, a Linux x86_64 tarball, and a macOS arm64 tarball.
