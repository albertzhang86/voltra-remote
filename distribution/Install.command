#!/bin/sh
cd "$(dirname "$0")" || exit 1
python3 install.py
result=$?
printf '\nPress Enter to close.'
read answer
exit "$result"
