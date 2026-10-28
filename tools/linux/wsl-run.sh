#!/bin/bash
# run.sh: starts ~/lm-a/build/launchme with a throwaway data folder, runs $SCENARIO (bash) against
# its X11 window, then prints its output and log.
#   MODE=tv|desktop  DURATION=seconds  WAIT=seconds before the scenario  DRIVER=x11|wayland  FRESH=1
#   XVFB=1 (default): a private Xvfb display (:91), so no window or keystroke reaches the Windows desktop.
#   XVFB=0: WSLg (its X11 or Wayland), visible on the desktop: never send keys there.
SHOTS=/mnt/c/Users/pogor/AppData/Local/Temp/opencode/lm/shots
mkdir -p $SHOTS
if [ "${XVFB:-1}" = "1" ]; then
  export DISPLAY=:91
  unset WAYLAND_DISPLAY
  if ! xdpyinfo -display :91 > /dev/null 2>&1; then
    setsid -f Xvfb :91 -screen 0 ${SCREEN:-1920x1080x24} -nolisten tcp > /dev/null 2>&1 < /dev/null
    sleep 1.5
  fi
  export SDL_VIDEO_DRIVER=x11
else
  export DISPLAY=${DISPLAY:-:0} WAYLAND_DISPLAY=${WAYLAND_DISPLAY:-wayland-0}
  [ -S "$XDG_RUNTIME_DIR/wayland-0" ] || export XDG_RUNTIME_DIR=/mnt/wslg/runtime-dir
  export SDL_VIDEO_DRIVER=${DRIVER:-x11}
fi
export LAUNCHME_CONFIG_DIR=$HOME/lm/data
[ "$FRESH" = "1" ] && rm -rf $LAUNCHME_CONFIG_DIR
mkdir -p $LAUNCHME_CONFIG_DIR
if [ ! -f $LAUNCHME_CONFIG_DIR/settings.ini ] || [ -n "$MODE" ]; then
  TV=1; [ "$MODE" = "desktop" ] && TV=0
  printf 'launch_tv_mode=%s\ntour_done=1\naurelia_intro_done=1\nupdate_check=0\n%s' "$TV" "$EXTRA_SETTINGS" > $LAUNCHME_CONFIG_DIR/settings.ini
fi
rm -f $LAUNCHME_CONFIG_DIR/launchme.log
cd $HOME/lm/build
${RUNNER} timeout ${DURATION:-25} ./launchme $ARGS > $HOME/lm/run.out 2>&1 &
PID=$!
sleep ${WAIT:-6}
LPID=$(pgrep -n -x launchme)
WID=$(xdotool search --pid "$LPID" 2>/dev/null | tail -1)
echo "pid=$LPID window=$WID display=$DISPLAY"
shot() { if [ -n "$WID" ]; then import -window "$WID" $SHOTS/$1.png; else import -window root $SHOTS/$1.png; fi; echo "shot $1"; }
# Keys only on the private display (on WSLg they would take the user's keyboard focus).
key() { [ "${XVFB:-1}" = "1" ] || { echo "no keys on WSLg"; return; }; xdotool windowfocus "$WID" 2>/dev/null; xdotool key --delay 80 "$@"; sleep ${KEYWAIT:-0.6}; }
click() { [ "${XVFB:-1}" = "1" ] || return; xdotool mousemove --window "$WID" "$1" "$2" click 1; sleep ${KEYWAIT:-0.6}; }
cpu() { top -b -n ${2:-1} -d ${3:-3} -p "$LPID" | grep -E "launchme|PID" | tail -1; }
eval "$SCENARIO"
wait $PID
echo "exit=$?"
echo "---- output"; tail -${OUT:-40} $HOME/lm/run.out
echo "---- log"; tail -${OUT:-40} $LAUNCHME_CONFIG_DIR/launchme.log 2>/dev/null
ls $LAUNCHME_CONFIG_DIR/crashes 2>/dev/null && tail -50 $LAUNCHME_CONFIG_DIR/crashes/* 2>/dev/null | head -80
