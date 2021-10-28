#!/bin/sh
modprobe fuse
cd /usr/src/vkQuake/AppImage
make -C ../Quake clean
make -C ../Quake -j
rm -rf AppDir
FOLDER=vkquake${VERSION}_linux64
ARCHIVE=${FOLDER}.tar.gz
rm -rf vkquake*
mkdir ${FOLDER}
./linuxdeploy-x86_64.AppImage -e ../Quake/vkquake --appdir=AppDir --create-desktop-file -i ../Misc/vkQuake_256.png --icon-filename=vkquake --output appimage
cp vkquake-x86_64.AppImage ${FOLDER}/vkquake.AppImage
cp ../Quake/vkquake.pak ${FOLDER}
cp ../LICENSE.txt ${FOLDER}
tar -zcvf ${ARCHIVE} ${FOLDER}
