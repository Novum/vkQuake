#/bin/sh
cd ../..

rm -rf build/darwin-arm64
meson setup build/darwin-arm64 -Dbuildtype=release -Db_lto=true --cross-file Packaging/macOS/arm64-darwin.txt
ninja -C build/darwin-arm64

rm -rf build/darwin-x86_64
export LIBRARY_PATH=/usr/local/lib/:$LIBRARY_PATH
meson setup build/darwin-x86_64 -Dbuildtype=release -Db_lto=true --cross-file Packaging/macOS/x86_64-darwin.txt
ninja -C build/darwin-x86_64

lipo -create -output Packaging/macOS/vkquake build/darwin-arm64/vkquake build/darwin-x86_64/vkquake
