#!/bin/bash
set -euo pipefail

FOLDER=vkquake-${VERSION}_linux64
ARCHIVE=$FOLDER.tar.gz

cd /usr/src/vkQuake

rm -rf build/appimage

python3 /opt/meson/meson.py build/appimage -Dbuildtype=release -Db_lto=true -Dmp3_lib=mad
ninja -C build/appimage

cd Packaging/AppImage
rm -rf AppDir
rm -rf vkquake*
mkdir "$FOLDER"
./linuxdeploy-x86_64.AppImage \
	-e ../../build/appimage/vkquake --appdir=AppDir -d ../../Misc/vkquake.desktop \
	-i ../../Misc/vkQuake_256.png --icon-filename=vkquake --output appimage

cp "vkQuake-$VERSION-x86_64.AppImage" "$FOLDER/vkquake.AppImage"
cp ../../LICENSE.txt "$FOLDER"
tar -zcvf "$ARCHIVE" "$FOLDER"
