#!/usr/bin/env python3

import enum
import sys
import argparse
import requests
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
        self, url: str, dir: str, default: bool, quiet: bool, verbose: bool
    ) -> None:
        self.log = "\033[1;33m[LOG]\033[0m "
        self.error = "\033[31m[ERROR]\033[0m "
        self.input = "\033[1;34m[INPUT]\033[m "
        if "downloads.khinsider.com" not in url:
            print(f"{self.log}  URL not matching khinsider")
            print(f"{self.error}Invalid URL")
            sys.exit(1)
        else:
            self.url = url
        if dir is not None:
            g = 0
        else:
            g = 1
        self.default = default
        if quiet and verbose:
            print(f"{self.error}Quiet and Verbose cannot be used at the same time")
            sys.exit(1)
        self.quiet = quiet
        self.verbose = verbose

    def filetype_input(self, type_list: list) -> None:
        print(f"{self.input}Enter a number a press Return/Enter")
        for i, filetype in enumerate(type_list):
            default = ""
            if filetype == type_list[-1]:
                default = "(Default)"
            print(f"{i}) {filetype} {default}")

    def do_nothing(self):
        return

    def scraper(self):
        headers = {
            "User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/139.0.0.0 Safari/537.36"
        }
        print(f"{self.log}Loading Webpage") if not self.quiet else self.do_nothing()
        page = requests.get(self.url, headers=headers)
        soup = bs(page.content, "html.parser")

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
            s.find("img")["src"]
            for s in soup.find_all("div", attrs={"class": "albumImage"})
        ]

        if not self.default:
            print()
            self.filetype_input(soup_file)
            while True:
                try:
                    select = input(": ")
                    soup_filetype = int(select)
                    if (len(soup_file) - 1) < soup_filetype:
                        raise ValueError(
                            f"{soup_filetype} is larger than the list length"
                        )
                    else:
                        print()
                        break
                except Exception as err:
                    print(f"{self.error}Not a valid input")
                    print()
                    self.filetype_input(soup_file)

        print(f"{self.log}Loading Songs") if not self.quiet else self.do_nothing()
        soup_songlist = soup.find("table", attrs={"id": "songlist"}).find_all("tr")
        soup_songs = []
        for i in soup_songlist:
            cd = i.find_all("td", attrs={"align": "center"})
            if cd == []:
                continue
            id = i.find_all("td", attrs={"align": "right"})
            key = i.find_all("td", attrs={"class": "clickable-row"})[0].find("a")

            disk = cd[-1].text
            track = id[0].text[:-1]
            title = key.text
            url = f"https://downloads.khinsider.com{key['href']}"

            soup_songs.append([disk, track, title, url])


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
        print("\033[1;32m[INTERRUPT]\033[0m Process Stopped")
        sys.exit(1)
