#!/bin/sh
modprobe fuse
cd /usr/src/vkQuake/AppImage
make -C ../Quake clean
make -C ../Quake -j
rm -rf AppDir
./linuxdeploy-x86_64.AppImage -e ../Quake/vkquake --appdir=AppDir --create-desktop-file -i ../Misc/vkQuake_256.png --icon-filename=vkquake --output appimage
