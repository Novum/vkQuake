#!/bin/bash
set -euo pipefail

FOLDER=vkquake-${VERSION}_linux64
ARCHIVE=$FOLDER.tar.gz

cd /usr/src/vkQuake

rm -rf build/appimage

python3 /opt/meson/meson.py build/appimage -Ddebug=true -Dstrip=false -Dmp3_lib=mad
ninja -C build/appimage

BIN="build/appimage/vkquake"
DBG="build/appimage/vkquake.debuginfo"

# Export the debug information into a separate file
objcopy --only-keep-debug "$BIN" "$DBG"

# strip the generated binary
strip --strip-debug --strip-unneeded "$BIN"

cd Packaging/AppImage
rm -rf AppDir
rm -rf vkquake*
mkdir "$FOLDER"
./linuxdeploy-x86_64.AppImage \
	-e ../../build/appimage/vkquake --appdir=AppDir -d ../../Misc/vkquake.desktop \
	-i ../../Misc/vkQuake_256.png --icon-filename=vkquake --output appimage

cp "vkQuake-$VERSION-x86_64.AppImage" "$FOLDER/vkquake.AppImage"
cp ../../LICENSE.txt "$FOLDER"
cp ../../build/appimage/vkquake.debuginfo "$FOLDER/"
cp Reading_Stack_Traces_HOWTO_Linux.md "$FOLDER/"
tar -zcvf "$ARCHIVE" "$FOLDER"

