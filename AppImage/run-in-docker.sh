#!/bin/sh
modprobe fuse
cd /usr/src/vkQuake/AppImage
make -C ../Quake clean
make -C ../Quake -j
rm -rf AppDir
rm -rf vkquake_linux64
rm -f vkquake_linux64.tar.gz
mkdir vkquake_linux64
./linuxdeploy-x86_64.AppImage -e ../Quake/vkquake --appdir=AppDir --create-desktop-file -i ../Misc/vkQuake_256.png --icon-filename=vkquake --output appimage
cp vkquake-x86_64.AppImage vkquake_linux64
cp ../Quake/vkquake.pak vkquake_linux64
tar -zcvf vkquake_linux64.tar.gz vkquake_linux64
