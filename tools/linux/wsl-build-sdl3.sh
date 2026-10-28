#!/bin/bash
set -e
mkdir -p /opt/src && cd /opt/src
if [ ! -d SDL ]; then
  git clone --depth 1 --branch release-3.4.18 https://github.com/libsdl-org/SDL.git
fi
cmake -S SDL -B SDL/build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr/local \
  -DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF > /root/sdl-configure.log 2>&1
cmake --build SDL/build > /root/sdl-build.log 2>&1
cmake --install SDL/build > /root/sdl-install.log 2>&1
ldconfig
pkg-config --modversion sdl3
