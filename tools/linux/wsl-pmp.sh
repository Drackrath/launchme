#!/bin/bash
# Poor man's profiler: samples the main thread of the running launchme N times; prints the
# innermost library frame and the first launcher frames of each sample.
PID=$(pgrep -n -x launchme)
N=${1:-20}
for i in $(seq 1 $N); do
  gdb -batch -p $PID -ex "set pagination off" -ex "thread 1" -ex "bt 60" 2>/dev/null > /tmp/pmp.one
  top=$(grep -E "^#0 " /tmp/pmp.one | sed -E 's/^#0 +(0x[0-9a-f]+ in )?//; s/ \(.*$//')
  ours=$(grep -E "^#" /tmp/pmp.one | grep -E " at /home/pp/lm/src/" | sed -E 's/^#[0-9]+ +(0x[0-9a-f]+ in )?//; s/ \(.*$//' | head -${DEPTH:-6} | tr '\n' '<')
  lib=$(grep -E "^#" /tmp/pmp.one | grep -vE " at /home/pp/lm/src/" | grep -E "cairo_|pango_|pixman_|SDL_|gl[A-Z]|_mesa" | head -2 | sed -E 's/^#[0-9]+ +(0x[0-9a-f]+ in )?//; s/ \(.*$//' | tr '\n' '<')
  echo "$top | $lib | $ours"
  sleep 0.15
done | sort | uniq -c | sort -rn | head -40
