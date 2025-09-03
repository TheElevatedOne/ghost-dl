#!/usr/bin/env python3

# built-in imports
import os
import gc
import sys
import time
import argparse
import multiprocessing

import os.path as op

# library imports
import psutil
import requests
from tqdm import tqdm
from bs4 import BeautifulSoup as bs


def arg_parser() -> argparse.Namespace:
    "Argument Parser using the argparse library"
    parser = argparse.ArgumentParser()

    parser.add_argument(
        "-u",
        "--url",
        required=True,
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
        "-t",
        "--threads",
        required=False,
        type=int,
        default=(psutil.cpu_count() // 2) if psutil.cpu_count() is not None else 1,
        help=f"Number of download threads (default = {(psutil.cpu_count() // 2) if psutil.cpu_count() is not None else 1}; CPU Threads / 2)",
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
    """Kingdom Hearts Insider Game OST Downloader"""

    def __init__(
        self, url: str, dir, default: bool, quiet: bool, verbose: bool, threads: int
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
        self.quiet: bool = quiet
        self.verbose: bool = verbose
        self.threads: int = threads
        self.manager = multiprocessing.Manager()
        self.headers: dict = {
            "User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/139.0.0.0 Safari/537.36"
        }

        self.scraper()

    def filetype_input(self, type_list: list[str]) -> None:
        """Input Checking Loop"""
        print(f"{self.input}Enter a number a press Return/Enter")
        for i, filetype in enumerate(type_list):
            default = ""
            if filetype == type_list[-1]:
                default = "(Default)"
            print(f"{i}) {filetype} {default}")

    def do_nothing(self) -> None:
        """Do Nothing, Return Nothing"""
        return

    def chunks(self, lst: list, n: int) -> list:
        """From a list create a matrix with n list chunks"""
        h = len(lst) // n
        temp_chunk = [lst[i : i + h] for i in range(0, len(lst), h)]
        if len(temp_chunk[-1]) == 1 and len(temp_chunk) > 1:
            temp_chunk[-2].append(temp_chunk[-1][0])
            temp_chunk.pop()
            return temp_chunk
        return temp_chunk

    def song_scraper(self, urls: list[str], filetype: str, shared_list: list) -> None:
        """Scrape song download urls"""
        for url in urls:
            song_soup = bs(
                requests.get(url, headers=self.headers).content, "html.parser"
            )
            song_links: list[str] = [
                s["href"] for s in song_soup.find_all("a") if "vgmsite" in str(s)
            ]
            song_link = [s for s in song_links if filetype in s][0]
            shared_list.append([url, song_link])

    def parallel_song_dl(self, chunk: list[dict], directory: str, dl_count: list):
        for song in chunk:
            self.downloader(
                url=song["download"],
                directory=directory,
                title=f"{song['disk']}{song['track']}. {song['title']}",
                extention=song["type"],
            )
            dl_count.append(0)

    def downloader(
        self,
        url: str,
        directory: str,
        title: str,
        extention: str,
        dl_count: list = [None],
    ) -> None:
        """Download URL to a file"""
        with open(op.join(directory, f"{title}.{extention}"), "wb") as f:
            response = requests.get(url, stream=True)
            f.write(response.content)
            try:
                if dl_count[0] is not None:
                    dl_count.append(0)
            except Exception:
                dl_count.append(0)

    def webpage_scraper(self, url: str) -> dict:
        start_timer: float = time.perf_counter()
        page = requests.get(url, headers=self.headers)
        soup = bs(page.content, "html.parser")

        soup_title = soup.find_all("h2")[0].text
        soup_year: str = (
            [
                s
                for s in str(soup.find("p", attrs={"align": "left"}).text).split("\n")
                if "Year" in s
            ][0]
            .split(" ")[-1]
            .strip()
        )
        soup_type: str = (
            [
                s
                for s in str(soup.find("p", attrs={"align": "left"}).text).split("\n")
                if "Album type" in s
            ][0]
            .split(" ")[-1]
            .strip()
        )
        soup_file: list[str] = [
            d.replace("(", "").replace(")", "").replace(",", "").strip().lower()
            for d in [
                s
                for s in str(soup.find("p", attrs={"align": "left"}).text).split("\n")
                if "Total Filesize:" in s
            ][0].split(" ")
            if "(" in d
        ]
        soup_fulltitle: str = f"{soup_year} - {soup_title} ({soup_type})"
        soup_albumart: list = [
            s.find("a")["href"]
            for s in soup.find_all("div", attrs={"class": "albumImage"})
        ]

        end_timer: float = time.perf_counter()
        return {
            "title": soup_title,
            "year": soup_year,
            "type": soup_type,
            "filetype": soup_file,
            "fulltitle": soup_fulltitle,
            "albumart": soup_albumart,
            "timer": round(end_timer - start_timer, 2),
            "soup": soup,
        }

    def scraper(self) -> None:
        """Main Scraper"""
        # Scrape the base webpage
        sys.stdout.write(
            f"{self.log}Loading Webpage"
        ) if not self.quiet else self.do_nothing()
        sys.stdout.flush() if not self.quiet else self.do_nothing()
        scrap = self.webpage_scraper(self.url)
        sys.stdout.write(
            f" [{scrap['timer']}s]\n"
        ) if not self.quiet else self.do_nothing()
        sys.stdout.flush() if not self.quiet else self.do_nothing()

        print(
            f"{self.log}Album title: {scrap['title']}"
        ) if self.verbose else self.do_nothing()
        print(
            f"{self.log}Album year: {scrap['year']}"
        ) if self.verbose else self.do_nothing()
        print(
            f"{self.log}Album type: {scrap['type']}"
        ) if self.verbose else self.do_nothing()

        # Filetype Input Loop
        soup_filetype = scrap["filetype"][-1]
        if not self.default:
            print()
            self.filetype_input(scrap["filetype"])
            while True:
                try:
                    select = input(":")
                    soup_filetype = int(select)
                    if (len(scrap["filetype"]) - 1) < soup_filetype:
                        raise ValueError(
                            f"{soup_filetype} is larger than the list length"
                        )
                    else:
                        soup_filetype = scrap["filetype"][soup_filetype]
                        print()
                        break
                except Exception:
                    print(f"{self.error}Not a valid input")
                    print()
                    self.filetype_input(scrap["filetype"])

        # Getting song information and scraping their download url
        sys.stdout.write(
            f"{self.log}Loading Songs (This will take a while)"
        ) if not self.quiet else self.do_nothing()
        sys.stdout.flush() if not self.quiet else self.do_nothing()
        start: float = time.perf_counter()
        soup_songlist = (
            scrap["soup"].find("table", attrs={"id": "songlist"}).find_all("tr")
        )
        soup_songs = []
        for q, i in enumerate(soup_songlist):
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

            soup_songs.append(
                {
                    "disk": disk,
                    "track": track,
                    "title": title,
                    "type": soup_filetype,
                    "url": url,
                }
            )

        urls = [x["url"] for x in soup_songs]
        urls_chunked = self.chunks(urls, self.threads * 2)
        jobs = []
        download_links_matx = self.manager.list()
        for i, s in enumerate(urls_chunked):
            j = multiprocessing.Process(
                target=self.song_scraper,
                args=(s, soup_filetype, download_links_matx),
            )
            jobs.append(j)
            j.start()
        for j in jobs:
            j.join()

        for q, i in enumerate(soup_songs):
            for j in download_links_matx:
                if j[0] == i["url"]:
                    soup_songs[q]["download"] = j[1]
        end: float = time.perf_counter()
        sys.stdout.write(
            f" [{round(end - start, 2)}s]\n"
        ) if not self.quiet else self.do_nothing()
        sys.stdout.flush() if not self.quiet else self.do_nothing()

        # Downloading Album Arts
        print(
            f"{self.log}Preparing Download Folder"
        ) if not self.quiet else self.do_nothing()
        os.mkdir(op.join(self.dir, scrap["fulltitle"])) if not op.isdir(
            op.join(self.dir, scrap["fulltitle"])
        ) else self.do_nothing()
        jobs = []
        dl_count = self.manager.list()
        for i, url in enumerate(scrap["albumart"]):
            j = multiprocessing.Process(
                target=self.downloader,
                args=(
                    url,
                    op.join(self.dir, scrap["fulltitle"]),
                    f"cover_{i}",
                    op.splitext(url)[-1].replace(".", ""),
                    dl_count,
                ),
            )
            jobs.append(j)
            j.start()

        art_bar = tqdm(
            enumerate(scrap["albumart"]),
            disable=False if not self.quiet else True,
            leave=True,
            bar_format="{desc} {percentage:3.0f}%|{bar}| {n_fmt}/{total_fmt} [{elapsed} | {rate_fmt}{postfix}]",
            total=len(scrap["albumart"]),
            desc=f"{self.log}Downloading Covers",
        )

        while len(dl_count) < len(scrap["albumart"]):
            start_value = len(dl_count)
            time.sleep(0.5)
            end_value = len(dl_count)
            if end_value != start_value:
                art_bar.update()
        del dl_count
        if art_bar.n < len(scrap["albumart"]):
            art_bar.update(len(scrap["albumart"]) - art_bar.n)
        art_bar.close()

        # Downloading Songs
        song_chunks = self.chunks(soup_songs, self.threads)
        dl_count = self.manager.list()
        jobs = []
        for i in song_chunks:
            j = multiprocessing.Process(
                target=self.parallel_song_dl,
                args=(i, op.join(self.dir, scrap["fulltitle"]), dl_count),
            )
            jobs.append(j)
            j.start()

        song_bar = tqdm(
            enumerate(soup_songs),
            disable=False if not self.quiet else True,
            leave=True,
            bar_format="{desc} {percentage:3.0f}%|{bar}| {n_fmt}/{total_fmt} [{elapsed} | {rate_fmt}{postfix}]",
            total=len(soup_songs),
            desc=f"{self.log}Downloading Songs",
        )

        while len(dl_count) < len(soup_songs):
            start_value = len(dl_count)
            time.sleep(0.5)
            end_value = len(dl_count)
            if end_value != start_value:
                song_bar.update()
        del dl_count
        if song_bar.n < len(soup_songs):
            song_bar.update(len(soup_songs) - song_bar.n)
        song_bar.close()

        print(f"{self.final}Album Download Complete")


if __name__ == "__main__":
    try:
        args = arg_parser()
        dl = GhostDL(
            url=args.url,
            dir=args.output,
            default=args.default,
            quiet=args.quiet,
            verbose=args.verbose,
            threads=args.threads,
        )
    except KeyboardInterrupt:
        print()
        print("\033[38;2;229;80;26m[INTERRUPT]\033[0m Process Stopped")
        sys.exit(1)
    except Exception as e:
        raise Exception(e)
    print()
