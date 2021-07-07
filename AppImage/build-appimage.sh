#/bin/sh
make -C ../Quake clean
make -C ../Quake -j
rm -rf AppDir
./linuxdeploy-x86_64.AppImage -e ../Quake/vkquake --appdir=AppDir --create-desktop-file -i ../Misc/vkQuake_256.png --icon-filename=vkquake
rm vkquake-x86_64.AppImage
./appimagetool-x86_64.AppImage AppDir --runtime-file AppDir/usr/bin/vkquake
