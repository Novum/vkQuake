# Download base image ubuntu 16.04
FROM ubuntu:16.04

RUN apt-get update
RUN apt-get install -y apt-transport-https ca-certificates wget

RUN wget -qO - https://packages.lunarg.com/lunarg-signing-key-pub.asc | apt-key add -
RUN wget -qO /etc/apt/sources.list.d/lunarg-vulkan-1.2.141-xenial.list https://packages.lunarg.com/vulkan/1.2.141/lunarg-vulkan-1.2.141-xenial.list

RUN apt-get update
RUN apt-get install -y --install-suggests \
	build-essential \
	file \
	fuse \
	libasound2-dev \
	libaudiofile-dev \
	libdbus-1-dev \
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
	vulkan-sdk \
	zlib1g-dev

RUN cd /tmp \
 && wget https://www.libsdl.org/release/SDL2-2.0.18.tar.gz \
 && tar -xzf SDL2-2.0.18.tar.gz \
 && cd /tmp/SDL2-2.0.18 \
 && ./configure \
 && make -j \
 && make install
