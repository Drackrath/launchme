#!/bin/bash
# Builds Skia (Ganesh: Vulkan + OpenGL, SkParagraph, SkShaper, SkUnicode on ICU) from source into
# $SKIA_DIR (default ~/skia) for the Linux launcher's renderer (src/linux/gfx). CMake finds it
# through LAUNCHME_SKIA_DIR. Uses the system FreeType, fontconfig, HarfBuzz, ICU and zlib; no image
# codecs (launchme decodes its pictures itself), no PDF/SVG, no Dawn/Graphite.
#   SKIA_BRANCH=chrome/m158  SKIA_DIR=~/skia  SKIA_JOBS=$(nproc)
set -euo pipefail
SKIA_DIR="${SKIA_DIR:-$HOME/skia}"
SKIA_BRANCH="${SKIA_BRANCH:-chrome/m158}"
if [ ! -d "$SKIA_DIR/.git" ]; then
  git clone --depth 1 --branch "$SKIA_BRANCH" https://github.com/google/skia.git "$SKIA_DIR"
fi
cd "$SKIA_DIR"
export GIT_SYNC_DEPS_SKIP_EMSDK=1
python3 tools/git-sync-deps
[ -x bin/gn ] || python3 bin/fetch-gn
# RTTI on: the launcher (g++, RTTI) derives from Skia classes (SkRefCnt, ParagraphPainter).
# PIC: Ubuntu's g++ links position-independent executables.
ARGS='is_official_build=true is_debug=false cc="clang" cxx="clang++"
 extra_cflags=["-fPIC","-O2"] extra_cflags_cc=["-frtti"]
 skia_use_vulkan=true skia_use_gl=true skia_use_egl=false skia_use_x11=false
 skia_enable_ganesh=true skia_enable_graphite=false skia_use_dawn=false
 skia_use_fontconfig=true skia_use_freetype=true skia_use_system_freetype2=true
 skia_use_harfbuzz=true skia_use_system_harfbuzz=true skia_use_icu=true skia_use_system_icu=true
 skia_enable_skparagraph=true skia_enable_skshaper=true skia_enable_skunicode=true
 skia_enable_pdf=false skia_enable_svg=false skia_enable_tools=false
 skia_use_partition_alloc=false skia_use_perfetto=false
 skia_use_libjpeg_turbo_decode=false skia_use_libjpeg_turbo_encode=false
 skia_use_libpng_decode=false skia_use_libpng_encode=false
 skia_use_libwebp_decode=false skia_use_libwebp_encode=false skia_use_wuffs=false
 skia_use_dng_sdk=false skia_use_piex=false skia_use_expat=false skia_use_xps=false'
bin/gn gen out/Release --args="$(echo $ARGS)"
TARGETS=$(bin/gn ls out/Release | grep -E '^//(:skia|modules/(skparagraph|skshaper|skunicode)(:[a-z_]+)?)$' | grep -vE 'tests|bench|slides|samples|utils' | sed 's#^//:##; s#^//##' | tr '\n' ' ')
echo "targets: $TARGETS"
ninja -C out/Release -j "${SKIA_JOBS:-$(nproc)}" $TARGETS pathops
# Path ops (CombineWithGeometry) are a source set only the PDF backend links: archived here.
rm -f out/Release/libpathops.a
find out/Release/obj/src/pathops -name 'pathops.*.o' -print0 | xargs -0 ar rcs out/Release/libpathops.a
ls -la out/Release/*.a
# The defines and system libraries a client of these libraries needs (cmake/LinuxApp.cmake reads them).
# (skparagraph sees Skia's public configs and its own module's.)
bin/gn desc out/Release //modules/skparagraph:skparagraph defines | grep -vE '_IMPLEMENTATION|^NDEBUG$' | sort -u > out/Release/client_defines.txt
{ for t in //:skia //modules/skparagraph:skparagraph; do bin/gn desc out/Release "$t" libs; done; } | sort -u > out/Release/client_libs.txt
echo "skia ok: $SKIA_DIR ($(git rev-parse --short HEAD))"
