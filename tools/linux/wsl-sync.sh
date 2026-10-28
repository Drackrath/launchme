#!/bin/bash
# Mirror the Windows checkout into the WSL file system (fast builds), without build output.
# LM_ROOT selects the mirror (default ~/lm): each concurrent worker uses its own.
set -e
ROOT="${LM_ROOT:-$HOME/lm}"
mkdir -p "$ROOT"
rsync -a --delete --exclude '/build/' --exclude '/.git/' --exclude '/_llm_docs/' --exclude '/.opencode/' \
  --exclude '/local/' --exclude '*.flac' /mnt/c/Users/pogor/Desktop/Work/launchme/ "$ROOT/src/"
echo synced
