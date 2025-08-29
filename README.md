# ghost-dl [WIP]

### A CLI downloader for khinsiders Game OST Archive

---

## Preview

<https://github.com/user-attachments/assets/77b7bc61-1400-41f4-933e-684adadbb28d>

## Usage

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

- URL - a downloads.khinsiders url (ex. <https://downloads.khinsider.com/game-soundtracks/album/minecraft>)
- Output - a valid existing directory, will set to current directory if not set
- Default - Without this flag, the script will prompt the user to enter the file type to download (eg. mp3, flac, ogg, etc.). If set, it will select the highest quality files.
- Quiet - Supresses unneeded logging meassages
- Verbose - Logs more messages

## Development Setup

Requirements: Python3, Venv

```bash
git clone https://github.com/TheElevatedOne/ghost-dl.git
cd ghost-dl/
python3 -m venv venv
source venv/bin/activate
pip install -r requirements.txt
```
