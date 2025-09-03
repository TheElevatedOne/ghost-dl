#!/usr/bin/env bash

if [ ! -d "./build" ]; then mkdir build; fi
nuitka --onefile --follow-imports --main=ghost_dl.py --output-dir=build --output-filename=ghost-dl
