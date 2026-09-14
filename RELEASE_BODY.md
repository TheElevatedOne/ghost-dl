# Changelog

C rewrite of ghost-dl.

- Native C11 CLI and ncurses search TUI
- Search, album info lookup, and download from the TUI
- Detailed multi-file download progress (speed, ETA, per-file bars)
- Full CLI: `--search`, `--info`, `--format`, `--default`, `--dry-run`, `--no-cover`, batch files
- Robust KHInsider parsing (songlist headers, disc/track columns, vgmtreasurechest CDNs)
- Parallel page fetches and downloads via pthreads + libcurl
- Release packages: Debian `.deb`, RPM `.rpm`, pacman `.pkg.tar.zst`, and a Linux tarball
