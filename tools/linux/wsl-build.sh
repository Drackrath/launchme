#!/bin/bash
# Usage: [LM_ROOT=~/lm-x] build.sh [targets...]
# Syncs the checkout into $LM_ROOT/src, configures $LM_ROOT/build if needed, builds with -k 0,
# logs to $LM_ROOT/build.log and prints the distinct errors.
ROOT="${LM_ROOT:-$HOME/lm}"
LM_ROOT="$ROOT" bash /mnt/c/Users/pogor/Desktop/Work/launchme/tools/linux/wsl-sync.sh >/dev/null
cd "$ROOT"
if [ ! -f build/build.ninja ]; then
  # Shared downloads (ONNX Runtime, sentencepiece, SQLite, miniz) from the main mirror when present.
  DEPS=""
  [ -d "$HOME/lm/build/_deps" ] && [ "$ROOT" != "$HOME/lm" ] && DEPS="-DFETCHCONTENT_BASE_DIR=$HOME/lm-deps"
  if [ -n "$DEPS" ] && [ ! -d "$HOME/lm-deps" ]; then cp -r "$HOME/lm/build/_deps" "$HOME/lm-deps"; fi
  cmake -S src -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_CXX_COMPILER=g++ -DCMAKE_C_COMPILER=gcc $DEPS > configure.log 2>&1 || { tail -40 configure.log; exit 1; }
fi
cmake --build build -j ${LM_JOBS:-12} -- -k 0 "$@" > build.log 2>&1
code=$?
echo "build exit=$code"
grep -E "error:|Error |undefined reference" build.log | sed "s#$ROOT/src/##" | sort | uniq -c | sort -rn | head -${LINES_SHOWN:-60}
echo "errors: $(grep -c 'error:' build.log)  files: $(grep -E 'error:' build.log | cut -d: -f1 | sort -u | wc -l)"
