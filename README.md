# ghost-dl

### A CLI downloader for [Kingdom Hearts Insider Game OST Archive](https://downloads.khinsider.com)

![GitHub License](https://img.shields.io/github/license/TheElevatedOne/ghost-dl?style=for-the-badge) ![GitHub Downloads](https://img.shields.io/github/downloads/TheElevatedOne/ghost-dl/total?style=for-the-badge) ![GitHub Issues or Pull Requests](https://img.shields.io/github/issues/TheElevatedOne/ghost-dl?style=for-the-badge) ![GitHub Actions Workflow Status](https://img.shields.io/github/actions/workflow/status/TheElevatedOne/ghost-dl/ghost-dl.yml?style=for-the-badge)

[![AUR Version](https://img.shields.io/aur/version/ghost-dl-git?style=for-the-badge&logo=git&logoColor=white&label=AUR%20GHOST-DL-GIT)](https://aur.archlinux.org/packages/ghost-dl-git) [![AUR Version](https://img.shields.io/aur/version/ghost-dl-bin?style=for-the-badge&logo=archlinux&logoColor=white&label=AUR%20GHOST-DL-BIN)](https://aur.archlinux.org/packages/ghost-dl-bin) [![GitHub Release](https://img.shields.io/github/v/release/TheElevatedOne/ghost-dl?display_name=release&style=for-the-badge)](https://github.com/TheElevatedOne/ghost-dl/releases/latest)

![ghost-dl-logo](https://github.com/TheElevatedOne/ghost-dl/blob/main/assets/logo.png?raw=true)

---

# Preview

<https://github.com/user-attachments/assets/77b7bc61-1400-41f4-933e-684adadbb28d>

# Usage

### Binaries can be found in the [Releases](https://github.com/TheElevatedOne/ghost-dl/releases/latest) section

```
usage: ghost-dl [-h] [-o OUTPUT] [-t THREADS] [-d] [-q] [-v] INPUT

Positional:
  INPUT                 Song URL / Batch File

Help:
  -h, --help            Show a Help Message
  --version             Show Version

Optional:
  -o, --output OUTPUT   Output directory (Current if not set)
  -t, --threads THREADS
                        Number of download threads (default = 4; CPU Threads / 2)
  -d, --default         Select the highest quality files without manual input

Logging:
  -q, --quiet           Suppress Log Messages
  -v, --verbose         Show More Log Messages
```

- INPUT - a **downloads.khinsider url** (ex. <https://downloads.khinsider.com/game-soundtracks/album/minecraft>) or a **file with multiple urls in lines** for **batch processing**
- Output - a valid existing directory, will set to current directory if not set
- Threads - uses multithreading to speed up loading and downloading, defaults to CPU Cores (eg. CPU Threads / 2)
- Default - Without this flag, the script will prompt the user to enter the file type to download (eg. mp3, flac, ogg, etc.). If set, it will select the highest quality files.
- Quiet - Supresses unneeded logging meassages
- Verbose - Logs more messages

### Exceptions and Errors

Most errors are being caught, other than errors from multiprocessing, as after trying multiple solutions to catch exceptions in multiple processes nothing worked.

If someone more knowledgeable than me knows how to fix this, either create an Enhancement Issue or Create a Pull Request.

# Development Setup

## Linux

**Requirements:** Python3, Venv

```bash
git clone https://github.com/TheElevatedOne/ghost-dl.git
cd ghost-dl/
python3 -m venv venv
source venv/bin/activate
pip install -r requirements.txt
```

### Building

**Requirements:** patchelf, ccache

```bash
# run nuitka-build.sh
./nuitka-build.sh

# or manually

mkdir build
nuitka --onefile --follow-imports --main=ghost_dl.py --output-dir=build --output-filename=ghost-dl
```

---

The Original name was supposed to be `ost-dl` but that is kinda lame, so I went with **ghost-dl** which still incorporates the information along with an identity.
