#/bin/sh
VERSION=`sh get-version.sh`
docker build --tag=build-vkquake docker && \
docker run --rm --privileged --env VERSION=${VERSION} -v ${PWD}/../..:/usr/src/vkQuake build-vkquake /usr/src/vkQuake/Packaging/AppImage/run-in-docker.sh
