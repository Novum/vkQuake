FROM ubuntu:16.04
ARG DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
 && apt-get install -y apt-transport-https ca-certificates wget \
 && wget -qO - https://packages.lunarg.com/lunarg-signing-key-pub.asc | apt-key add - \
 && wget -qO /etc/apt/sources.list.d/lunarg-vulkan-1.2.141-xenial.list https://packages.lunarg.com/vulkan/1.2.141/lunarg-vulkan-1.2.141-xenial.list \
 && apt-get update \
 && apt-get install -y \
	build-essential \
	file \
	fuse \
	unzip \
	ninja-build \
	libasound2-dev \
	libaudiofile-dev \
	libdbus-1-dev \
	libibus-1.0-dev \
	libmad0-dev \
	libopenal-dev \
	libpulse-dev \
	libsndio-dev \
	libudev-dev \
	libusb-dev \
	libvorbis-dev \
	libvorbis-dev \
	libvulkan-dev \
	libx11-xcb-dev \
	libxcursor-dev \
	libxrandr-dev \
	libxt-dev \
	libxv-dev \
	libxxf86vm-dev \
	libflac-dev \
	libopus-dev \
	libopusfile-dev \
	vulkan-sdk \
	zlib1g-dev

RUN cd /tmp \
 && wget https://www.libsdl.org/release/SDL2-2.0.22.tar.gz \
 && tar -xzf SDL2-2.0.22.tar.gz \
 && cd /tmp/SDL2-2.0.22 \
 && ./configure \
 && make -j \
 && make install

RUN cd /tmp \
 && wget https://github.com/ninja-build/ninja/releases/download/v1.11.0/ninja-linux.zip \
 && unzip ninja-linux.zip \
 && cp ninja /usr/bin/ninja

RUN cd /opt \
 && wget https://github.com/mesonbuild/meson/releases/download/0.63.0rc1/meson-0.63.0rc1.tar.gz \
 && tar -xzf meson-0.63.0rc1.tar.gz \
 && mv meson-0.63.0rc1 meson
 
