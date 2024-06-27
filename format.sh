#/bin/sh
docker run --user $(id -u):$(id -g) -v `pwd`:`pwd` -w `pwd` -i --rm ghcr.io/jidicula/clang-format:17 -i Quake/*.c Quake/*.h Shaders/*.frag Shaders/*.vert Shaders/*.comp Shaders/*.inc Shaders/*.c Shaders/*.h
