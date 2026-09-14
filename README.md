# ghost-dl

### A CLI / TUI downloader for [Kingdom Hearts Insider Game OST Archive](https://downloads.khinsider.com)

![GitHub License](https://img.shields.io/github/license/TheElevatedOne/ghost-dl?style=for-the-badge) ![GitHub Downloads](https://img.shields.io/github/downloads/TheElevatedOne/ghost-dl/total?style=for-the-badge) ![GitHub Issues or Pull Requests](https://img.shields.io/github/issues/TheElevatedOne/ghost-dl?style=for-the-badge) ![GitHub Actions Workflow Status](https://img.shields.io/github/actions/workflow/status/TheElevatedOne/ghost-dl/ghost-dl.yml?style=for-the-badge) ![Static Badge](https://img.shields.io/badge/Vibecoded-Grok-black?style=for-the-badge&labelColor=%236F0E82)


[![AUR Version](https://img.shields.io/aur/version/ghost-dl-git?style=for-the-badge&logo=git&logoColor=white&label=AUR%20GHOST-DL-GIT)](https://aur.archlinux.org/packages/ghost-dl-git) [![AUR Version](https://img.shields.io/aur/version/ghost-dl-bin?style=for-the-badge&logo=archlinux&logoColor=white&label=AUR%20GHOST-DL-BIN)](https://aur.archlinux.org/packages/ghost-dl-bin) [![GitHub Release](https://img.shields.io/github/v/release/TheElevatedOne/ghost-dl?display_name=release&style=for-the-badge)](https://github.com/TheElevatedOne/ghost-dl/releases/latest)

![ghost-dl-logo](https://github.com/TheElevatedOne/ghost-dl/blob/main/assets/logo.png?raw=true)

---

# Preview

<https://github.com/user-attachments/assets/77b7bc61-1400-41f4-933e-684adadbb28d>

# Usage

### Packages and binaries are in the [Releases](https://github.com/TheElevatedOne/ghost-dl/releases/latest) section

| Distro | Package | Install |
| --- | --- | --- |
| Debian / Ubuntu | `ghost-dl_*_amd64.deb` | `sudo apt install ./ghost-dl_*.deb` |
| Fedora / RHEL | `ghost-dl-*.x86_64.rpm` | `sudo dnf install ./ghost-dl-*.rpm` |
| Arch Linux | `ghost-dl-*-x86_64.pkg.tar.zst` | `sudo pacman -U ghost-dl-*.pkg.tar.zst` |

A generic `ghost-dl-*-linux-x86_64.tar.gz` tarball is also attached. Arch users can install from the AUR (`ghost-dl-git` or `ghost-dl-bin`).

```
usage: ghost-dl [OPTIONS] [INPUT...]

INPUT                 Album URL, album slug, or batch file

General:
  -h, --help          Show a help message
  -V, --version       Show version
      --tui           Open the search TUI
  -s, --search QUERY  Search albums and print results
  -i, --info          Show album info without downloading
  -n, --dry-run       Resolve files but do not download

Download:
  -o, --output DIR    Output directory (current if not set)
  -t, --threads N     Parallel workers (default = CPU/2, max 8)
  -f, --format FMT    Audio format, or comma-separated preference
                      (e.g. flac,mp3)
  -d, --default       Highest quality without prompting
  -y, --yes           Same as --default
      --no-cover      Skip album art
      --cover-only    Download only album art
      --force         Overwrite existing files

Logging:
  -q, --quiet         Suppress log messages
  -v, --verbose       Show more log messages
```

Run `ghost-dl` with no arguments to open the search TUI.

- **INPUT** - a `downloads.khinsider.com` album URL (e.g. <https://downloads.khinsider.com/game-soundtracks/album/minecraft>), the album slug (`minecraft`), or a **file with URLs/slugs, one per line** for batch processing
- **Search TUI** - type a query, press Enter, inspect album info, pick a format, and download
- **Output** - directory to write into (created if needed). Each album is stored in `YEAR - Title (Type)/`
- **Threads** - parallel page fetches and downloads (default CPU cores / 2, capped at 8)
- **Format** - `mp3`, `flac`, `ogg`, … If omitted, you are prompted. `--default` picks the highest quality available (FLAC over MP3, etc.)
- **Quiet / Verbose** - cannot be combined

### Examples

```bash
# Interactive search, info lookup, and download
ghost-dl

# Search from the CLI
ghost-dl --search "celeste"

# Download by slug or URL, highest quality, no prompt
ghost-dl -d -f flac minecraft
ghost-dl -d -o ~/Music https://downloads.khinsider.com/game-soundtracks/album/minecraft

# Inspect an album
ghost-dl --info persona-5

# Batch file (one URL or slug per line, # comments allowed)
ghost-dl -d albums.txt
```

### TUI keys

| Key | Action |
| --- | --- |
| type, Enter | Search |
| Esc | Return to the search bar |
| ↑ / ↓, j / k | Move selection |
| Enter / i | Load album info (and album art in Kitty / WezTerm / Ghostty) |
| f | Cycle audio format |
| d | Download selected album |
| / | Focus search |
| q | Quit |

Supported platforms: Linux and macOS. Windows users can build with MSYS2/MinGW or run under WSL.

# Development Setup

## Linux

**Requirements:** gcc, make, libcurl, ncursesw

```bash
# Debian / Ubuntu
sudo apt-get install build-essential libcurl4-openssl-dev libncurses-dev

# Fedora
sudo dnf install gcc make libcurl-devel ncurses-devel

# Arch
sudo pacman -S base-devel curl ncurses

git clone https://github.com/TheElevatedOne/ghost-dl.git
cd ghost-dl/
make
sudo make install   # optional, PREFIX=/usr/local
```

### Tests

Parser tests use saved KHInsider HTML fixtures and do not need the network:

```bash
make test
```

### Building a release binary

```bash
make
# binary: ./ghost-dl
```

### Building distro packages

```bash
make dist
# dist/ghost-dl_*_amd64.deb
# dist/ghost-dl-*.x86_64.rpm
# dist/ghost-dl-*-x86_64.pkg.tar.zst
# dist/ghost-dl-*-linux-x86_64.tar.gz
```

Requires `dpkg-deb`, `rpmbuild`, `zstd`, and `fakeroot` (on Arch: `pacman -S dpkg rpm-tools fakeroot`).

The original Python implementation is kept under `legacy/` for reference.

---

The original name was supposed to be `ost-dl` but that is kinda lame, so I went with **ghost-dl** which still incorporates the information along with an identity.
