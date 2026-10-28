#!/bin/bash
export MODE=${MODE:-tv} FRESH=${FRESH:-1} DURATION=40 WAIT=12 OUT=5
PERF=$(ls /usr/lib/linux-tools/*/perf | head -1)
export SCENARIO="shot perf_${MODE}; $PERF record -F 999 -g -p \$LPID -o /tmp/lm.perf -- sleep 12 >/dev/null 2>&1; $PERF report -i /tmp/lm.perf --no-children --sort dso,symbol --stdio 2>/dev/null | grep -E '^ +[0-9]' | head -45"
bash /mnt/c/Users/pogor/Desktop/Work/launchme/tools/linux/wsl-run.sh 2>&1 | grep -vE 'pw.conf|ALSA lib|libEGL|^$|MESA'
