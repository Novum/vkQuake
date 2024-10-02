FROM ubuntu:20.04
ARG DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
 && apt-get install -y apt-transport-https ca-certificates wget gnupg apt-utils \
 && wget -qO - https://packages.lunarg.com/lunarg-signing-key-pub.asc | apt-key add - \
 && wget -qO /etc/apt/sources.list.d/lunarg-vulkan-1.3.275-focal.list https://packages.lunarg.com/vulkan/1.3.275/lunarg-vulkan-1.3.275-focal.list \
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
	zlib1g-dev \
 && rm -rf /var/lib/apt/lists/*

RUN cd /tmp \
 && wget https://www.libsdl.org/release/SDL2-2.30.8.tar.gz \
 && tar -xzf SDL2-2.30.8.tar.gz \
 && rm SDL2-2.30.8.tar.gz \
 && cd /tmp/SDL2-2.30.8 \
 && ./configure \
 && make -j \
 && make install

RUN cd /tmp \
 && wget https://github.com/ninja-build/ninja/releases/download/v1.11.1/ninja-linux.zip \
 && unzip ninja-linux.zip \
 && rm ninja-linux.zip \
 && cp ninja /usr/bin/ninja

RUN cd /opt \
 && wget https://github.com/mesonbuild/meson/releases/download/1.3.1/meson-1.3.1.tar.gz \
 && tar -xzf meson-1.3.1.tar.gz \
 && rm meson-1.3.1.tar.gz \
 && mv meson-1.3.1 meson
 
