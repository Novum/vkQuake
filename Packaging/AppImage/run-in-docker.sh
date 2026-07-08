#!/bin/bash
set -euo pipefail

cd /usr/src/vkQuake

rm -rf build/appimage
rm -rf build/sdl2

python3 /opt/meson/meson.py setup build/appimage -Ddebug=true -Dstrip=false -Dmp3_lib=mad
ninja -C build/appimage

# Compile check the SDL2 backend (the AppImage ships SDL3)
python3 /opt/meson/meson.py setup build/sdl2 -Ddebug=true -Dstrip=false -Dmp3_lib=mad -Duse_sdl3=disabled
ninja -C build/sdl2

cd Packaging/AppImage
rm -rf AppDir
rm -rf vkquake* vkQuake-*

# NO_STRIP keeps the debug info in the binary for symbolizing crash
# reports; the sections are not loaded at runtime
NO_STRIP=1 ./linuxdeploy-x86_64.AppImage \
	-e ../../build/appimage/vkquake --appdir=AppDir -d ../../Misc/vkquake.desktop \
	-i ../../Misc/vkQuake_256.png --icon-filename=vkquake --output appimage
