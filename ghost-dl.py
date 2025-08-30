#!/usr/bin/env python3

import sys
import argparse
import requests
import os
import os.path as op
from bs4 import BeautifulSoup as bs


def arg_parser() -> argparse.Namespace:
    parser = argparse.ArgumentParser()

    parser.add_argument(
        "-u",
        "--url",
        required=False,
        type=str,
        help="URL of the Soundtrack to download",
    )
    parser.add_argument(
        "-o",
        "--output",
        required=False,
        type=str,
        help="Output directory (Current if not set)",
    )
    parser.add_argument(
        "-d",
        "--default",
        required=False,
        action="store_true",
        help="Select the highest quality files without manual input",
    )
    parser.add_argument(
        "-q",
        "--quiet",
        required=False,
        action="store_true",
        help="Suppress Log Messages",
    )
    parser.add_argument(
        "-v",
        "--verbose",
        required=False,
        action="store_true",
        help="Show more Log Messages",
    )

    return parser.parse_args()


class GhostDL:
    def __init__(
        self, url: str, dir, default: bool, quiet: bool, verbose: bool
    ) -> None:
        # Colored ANSI Strings
        self.log = "\033[1;33m[LOG]\033[0m "
        self.error = "\033[31m[ERROR]\033[0m "
        self.input = "\033[1;34m[INPUT]\033[0m "
        self.dl = "\033[38;2;255;0;255m[DL]\033[0m "
        self.done = "\033[38;2;0;255;40m[DONE]\033[0m "
        self.final = "\033[38;2;0;242;255m[FINAL]\033[0m "

        # Exit if khinsider url not matched
        if "downloads.khinsider.com" not in url:
            print(f"{self.log}  URL not matching khinsider")
            print(f"{self.error}Invalid URL")
            sys.exit(1)
        else:
            self.url = url
        # Output validity check
        if dir is not None:
            print(f"{self.log}Checking custom directory validity")
            if op.isdir(op.abspath(dir)):
                print(f"{self.log}Custom directory valid")
                self.dir = op.abspath(dir)
            else:
                print(f"{self.log}Custom directory invalid")
                print(f"{self.log}Selecting current directory")
                self.dir = os.getcwd()
        else:
            print(f"{self.log}Selecting current directory")
            self.dir = os.getcwd()
        # Default tag
        self.default = default
        # Verbose and quiet oppose each other, exit if it happends
        if quiet and verbose:
            print(f"{self.error}Quiet and Verbose cannot be used at the same time")
            sys.exit(1)
        self.quiet = quiet
        self.verbose = verbose

    def filetype_input(self, type_list: list) -> None:
        # Function as a workaround for input field loop
        print(f"{self.input}Enter a number a press Return/Enter")
        for i, filetype in enumerate(type_list):
            default = ""
            if filetype == type_list[-1]:
                default = "(Default)"
            print(f"{i}) {filetype} {default}")

    def do_nothing(self):
        # There is not a do nothing function
        # I don't know why
        return

    def scraper(self):
        # Headers are needed as it returns error otherwise
        headers = {
            "User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/139.0.0.0 Safari/537.36"
        }
        # Scrape the base webpage
        print(f"{self.log}Loading Webpage") if not self.quiet else self.do_nothing()
        page = requests.get(self.url, headers=headers)
        soup = bs(page.content, "html.parser")

        # Reading Title, Year, Album, Filetypes, Album arts from the HTML
        print(f"{self.log}Reading Metadata") if not self.quiet else self.do_nothing()
        soup_title = soup.find_all("h2")[0].text
        print(
            f"{self.log}Album title: {soup_title}"
        ) if self.verbose else self.do_nothing()
        soup_year = (
            [
                s
                for s in str(soup.find("p", attrs={"align": "left"}).text).split("\n")
                if "Year" in s
            ][0]
            .split(" ")[-1]
            .strip()
        )
        print(
            f"{self.log}Album year: {soup_year}"
        ) if self.verbose else self.do_nothing()
        soup_type = (
            [
                s
                for s in str(soup.find("p", attrs={"align": "left"}).text).split("\n")
                if "Album type" in s
            ][0]
            .split(" ")[-1]
            .strip()
        )
        print(
            f"{self.log}Album type: {soup_type}"
        ) if self.verbose else self.do_nothing()
        soup_file = [
            d.replace("(", "").replace(")", "").replace(",", "").strip().lower()
            for d in [
                s
                for s in str(soup.find("p", attrs={"align": "left"}).text).split("\n")
                if "Total Filesize:" in s
            ][0].split(" ")
            if "(" in d
        ]
        soup_fulltitle = f"{soup_year} - {soup_title} ({soup_type})"
        soup_albumart = [
            s.find("a")["href"]
            for s in soup.find_all("div", attrs={"class": "albumImage"})
        ]

        # Filetype Input Loop
        soup_filetype = soup_file[-1]
        if not self.default:
            print()
            self.filetype_input(soup_file)
            while True:
                try:
                    select = input(":")
                    soup_filetype = int(select)
                    if (len(soup_file) - 1) < soup_filetype:
                        raise ValueError(
                            f"{soup_filetype} is larger than the list length"
                        )
                    else:
                        soup_filetype = soup_file[soup_filetype]
                        print()
                        break
                except Exception:
                    print(f"{self.error}Not a valid input")
                    print()
                    self.filetype_input(soup_file)

        # Getting song information and scraping their download url
        print(
            f"{self.log}Loading Songs (This will take a while)"
        ) if not self.quiet else self.do_nothing()
        soup_songlist = soup.find("table", attrs={"id": "songlist"}).find_all("tr")
        soup_songs = []
        for i in soup_songlist:
            cd = i.find_all("td", attrs={"align": "center"})
            if cd == []:
                continue
            id = i.find_all("td", attrs={"align": "right"})
            key = i.find_all("td", attrs={"class": "clickable-row"})[0].find("a")

            disk = (
                cd[-1].text.strip()
                if cd[-1].text.strip() == ""
                else f"CD{cd[-1].text.strip()} "
            )
            track = id[0].text[:-1].strip()
            title = key.text.strip()
            url = f"https://downloads.khinsider.com{key['href']}"

            song_soup = bs(requests.get(url, headers=headers).content, "html.parser")
            song_links = [
                s["href"] for s in song_soup.find_all("a") if "vgmsite" in str(s)
            ]
            song_link = [s for s in song_links if soup_filetype in s][0]

            soup_songs.append([disk, track, title, song_link, url])

        # Downloading Album Arts
        print(f"{self.log}Preparing Download Folder")
        os.mkdir(op.join(self.dir, soup_fulltitle)) if not op.isdir(
            op.join(self.dir, soup_fulltitle)
        ) else self.do_nothing()
        print(f"{self.log}Starting Download of Album Art/s")
        for i, url in enumerate(soup_albumart):
            with open(op.join(self.dir, soup_fulltitle, f"cover_{i}.jpg"), "wb") as f:
                response = requests.get(url, stream=True)
                total_length = response.headers.get("content-length")

                if total_length is None:  # no content length header
                    f.write(response.content)
                else:
                    dl = 0
                    total_length = int(total_length)
                    for data in response.iter_content(chunk_size=4096):
                        dl += len(data)
                        f.write(data)
                        done = int(50 * dl / total_length)
                        sys.stdout.write(
                            f" {self.dl}Downloading Cover #{i:02}\r[%s%s]"
                            % ("=" * done, " " * (50 - done))
                        )
                        sys.stdout.flush()
            print(f" {self.done}Cover #{i:02} Downloaded")

        # Downloading Songs
        print()
        print(f"{self.log}Starting Download of Songs")
        for i in soup_songs:
            with open(
                op.join(
                    self.dir, soup_fulltitle, f"{i[0]}{i[1]}. {i[2]}.{soup_filetype}"
                ),
                "wb",
            ) as f:
                response = requests.get(i[3], stream=True)
                total_length = response.headers.get("content-length")

                if total_length is None:  # no content length header
                    f.write(response.content)
                else:
                    dl = 0
                    total_length = int(total_length)
                    for data in response.iter_content(chunk_size=4096):
                        dl += len(data)
                        f.write(data)
                        done = int(50 * dl / total_length)
                        sys.stdout.write(
                            f" {self.dl}Downloading Song {i[0]}{i[1]}. {i[2]}.{soup_filetype}\r[%s%s]"
                            % ("=" * done, " " * (50 - done))
                        )
                        sys.stdout.flush()
            print(f" {self.done}Song {i[0]}{i[1]}. {i[2]}.{soup_filetype} Downloaded")
        print()
        print(f"{self.final}Album Download Complete")
        print()


if __name__ == "__main__":
    try:
        args = arg_parser()
        dl = GhostDL(
            url=args.url,
            dir=args.output,
            default=args.default,
            quiet=args.quiet,
            verbose=args.verbose,
        )
        dl.scraper()
    except KeyboardInterrupt:
        print()
        print("\033[38;2;229;80;26m[INTERRUPT]\033[0m Process Stopped")
        sys.exit(1)
