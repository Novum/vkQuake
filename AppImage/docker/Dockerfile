# Download base image ubuntu 16.04
FROM ubuntu:16.04

RUN apt-get update
RUN apt-get install -y apt-transport-https ca-certificates wget

RUN wget -qO - https://packages.lunarg.com/lunarg-signing-key-pub.asc | apt-key add -
RUN wget -qO /etc/apt/sources.list.d/lunarg-vulkan-1.2.141-xenial.list https://packages.lunarg.com/vulkan/1.2.141/lunarg-vulkan-1.2.141-xenial.list

RUN apt-get update
RUN apt-get install -y build-essential libvulkan-dev libvorbis-dev libmad0-dev libx11-xcb-dev fuse vulkan-sdk file\
	libasound2-dev libpulse-dev libopenal-dev libsndio-dev libaudiofile-dev libvorbis-dev libusb-dev libdbus-1-dev zlib1g-dev

RUN cd /tmp \
 && wget https://www.libsdl.org/release/SDL2-2.0.18.tar.gz \
 && tar -xzf SDL2-2.0.18.tar.gz \
 && cd /tmp/SDL2-2.0.18 \
 && ./configure \
 && make -j \
 && make install
