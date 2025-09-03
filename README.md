# ghost-dl

### A CLI downloader for [Kingdom Hearts Insider Game OST Archive](https://downloads.khinsider.com)

[![AUR Version](https://img.shields.io/aur/version/ghost-dl-git?style=for-the-badge&logo=git&logoColor=white&label=AUR%20GHOST-DL-GIT)](https://aur.archlinux.org/packages/ghost-dl-git) [![AUR Version](https://img.shields.io/aur/version/ghost-dl-bin?style=for-the-badge&logo=archlinux&logoColor=white&label=AUR%20GHOST-DL-BIN)](https://aur.archlinux.org/packages/ghost-dl-bin) [![GitHub Release](https://img.shields.io/github/v/release/TheElevatedOne/ghost-dl?display_name=release&style=for-the-badge)](https://github.com/TheElevatedOne/ghost-dl/releases/latest)

![ghost-dl-logo](https://github.com/TheElevatedOne/ghost-dl/blob/main/assets/logo.png?raw=true)

---

# Preview

<https://github.com/user-attachments/assets/77b7bc61-1400-41f4-933e-684adadbb28d>

# Usage

### Binaries can be found in the [Releases](https://github.com/TheElevatedOne/ghost-dl/releases/latest) section

```
usage: ghost-dl [-h] [-u URL] [-o OUTPUT] [-d] [-q] [-v]

options:
  -h, --help           show this help message and exit
  -u, --url URL        URL of the Soundtrack to download
  -o, --output OUTPUT  Output directory (Current if not set)
  -d, --default        Select the highest quality files without manual input
  -q, --quiet          Suppress Log Messages
  -v, --verbose        Show more Log Messages
```

- URL - a downloads.khinsider url (ex. <https://downloads.khinsider.com/game-soundtracks/album/minecraft>)
- Output - a valid existing directory, will set to current directory if not set
- Default - Without this flag, the script will prompt the user to enter the file type to download (eg. mp3, flac, ogg, etc.). If set, it will select the highest quality files.
- Quiet - Supresses unneeded logging meassages
- Verbose - Logs more messages

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
