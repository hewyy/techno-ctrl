#!/bin/sh
set -eu

log_file="${LPS_LOG_FILE:-$HOME/Library/Hew/LivePatternSequencer/sequencer.log}"
filter="${1:-}"

if [ ! -e "$log_file" ]; then
    echo "Waiting for log file: $log_file" >&2
fi

if [ -n "$filter" ]; then
    tail -n 100 -F "$log_file" | grep --line-buffered -E "$filter"
else
    tail -n 100 -F "$log_file"
fi
