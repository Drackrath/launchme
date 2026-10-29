#!/bin/bash
# A tiny Windows program (mingw-w64) that writes how it was started (folder, arguments, the
# variables Launchme's Wine/Proton options set) to out.txt beside it, added as the custom game
# "Wine Probe" to the data folder of wsl-run.sh. Its prefix: ~/Games/launchme/prefixes/wine-probe.
set -e
DIR=$HOME/lm/wineprobe
CONFIG=${LAUNCHME_CONFIG_DIR:-$HOME/lm/data}
mkdir -p "$DIR" "$CONFIG"
cat > "$DIR/probe.c" <<'EOF'
#include <windows.h>
#include <stdio.h>
int main(int argc, char** argv) {
    FILE* out = fopen(argc > 1 ? argv[1] : "out.txt", "w");
    if (!out) return 2;
    char text[4096];
    GetCurrentDirectoryA(sizeof text, text);
    fprintf(out, "cwd=%s\n", text);
    for (int i = 0; i < argc; ++i) fprintf(out, "argv[%d]=%s\n", i, argv[i]);
    const char* names[] = {"WINEPREFIX", "WINEDLLOVERRIDES", "PULSE_LATENCY_MSEC", "DXVK_HUD", "LAUNCHME_GAME_KEY", "LAUNCHME_RUNNER",
                           "LAUNCHME_PREFIX", "PROBE_VAR", "WINEDEBUG", "WINEESYNC", "WINEFSYNC"};
    for (unsigned i = 0; i < sizeof names / sizeof *names; ++i) {
        const DWORD n = GetEnvironmentVariableA(names[i], text, sizeof text);
        fprintf(out, "%s=%s\n", names[i], n && n < sizeof text ? text : "<unset>");
    }
    fclose(out);
    Sleep(1500);
    return 0;
}
EOF
x86_64-w64-mingw32-gcc -O2 -o "$DIR/probe.exe" "$DIR/probe.c"
rm -f "$DIR/out.txt"
cat > "$CONFIG/custom-games.json" <<EOF
{"version":1,"games":[{"id":"0123456789abcdef","name":"Wine Probe","path":"$DIR/probe.exe","arguments":"out.txt -flag","directory":""}]}
EOF
echo "probe: $DIR/probe.exe"
