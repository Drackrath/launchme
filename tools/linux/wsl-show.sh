#!/bin/bash
# Starts the Linux launchme on the WSLg desktop (visible), with its own throwaway data folder.
# MODE=tv|desktop
export DISPLAY=${DISPLAY:-:0} WAYLAND_DISPLAY=${WAYLAND_DISPLAY:-wayland-0}
[ -S "$XDG_RUNTIME_DIR/wayland-0" ] || export XDG_RUNTIME_DIR=/mnt/wslg/runtime-dir
# X11 (XWayland): WSLg's Wayland shows GPU frames ~30x slower (src/linux/window/window.cpp).
export SDL_VIDEO_DRIVER=${DRIVER:-x11}
export LAUNCHME_CONFIG_DIR=$HOME/lm/data
mkdir -p "$LAUNCHME_CONFIG_DIR"
if [ ! -f "$LAUNCHME_CONFIG_DIR/settings.ini" ]; then
  [ -f "$HOME/lm-a/data/settings.ini" ] && cp -r "$HOME/lm-a/data/." "$LAUNCHME_CONFIG_DIR/"
fi
TV=1; [ "$MODE" = "desktop" ] && TV=0
if grep -q '^launch_tv_mode=' "$LAUNCHME_CONFIG_DIR/settings.ini" 2>/dev/null; then
  sed -i "s/^launch_tv_mode=.*/launch_tv_mode=$TV/" "$LAUNCHME_CONFIG_DIR/settings.ini"
else
  printf 'launch_tv_mode=%s\ntour_done=1\naurelia_intro_done=1\nupdate_check=0\n' "$TV" >> "$LAUNCHME_CONFIG_DIR/settings.ini"
fi
cd $HOME/lm/build
exec ./launchme > $HOME/lm/show.out 2>&1
