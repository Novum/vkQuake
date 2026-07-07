FROM ubuntu:20.04
ARG DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
 && apt-get install -y apt-transport-https ca-certificates wget gnupg apt-utils \
 && wget -qO - https://packages.lunarg.com/lunarg-signing-key-pub.asc | apt-key add - \
 && wget -qO /etc/apt/sources.list.d/lunarg-vulkan-1.3.275-focal.list https://packages.lunarg.com/vulkan/1.3.275/lunarg-vulkan-1.3.275-focal.list \
 && apt-get update \
 && apt-get install -y \
	build-essential \
	cmake \
	file \
	fuse \
	unzip \
	ninja-build \
	libasound2-dev \
	libaudiofile-dev \
	libcairo2-dev \
	libdbus-1-dev \
	libegl1-mesa-dev \
	libibus-1.0-dev \
	libmad0-dev \
	libopenal-dev \
	libpango1.0-dev \
	libpulse-dev \
	libsndio-dev \
	libudev-dev \
	libusb-dev \
	libvorbis-dev \
	libvulkan-dev \
	libwayland-dev \
	libx11-xcb-dev \
	libxcursor-dev \
	libxext-dev \
	libxfixes-dev \
	libxi-dev \
	libxkbcommon-dev \
	libxrandr-dev \
	libxss-dev \
	libxt-dev \
	libxtst-dev \
	libxv-dev \
	libxxf86vm-dev \
	libflac-dev \
	libopus-dev \
	libopusfile-dev \
	vulkan-sdk \
	wayland-protocols \
	zlib1g-dev \
 && rm -rf /var/lib/apt/lists/*

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

# headers for SDL's dynamically loaded libdecor support (window
# decorations on GNOME); focal has no libdecor package
RUN cd /tmp \
 && wget https://gitlab.freedesktop.org/libdecor/libdecor/-/archive/0.2.3/libdecor-0.2.3.tar.gz \
 && tar -xzf libdecor-0.2.3.tar.gz \
 && rm libdecor-0.2.3.tar.gz \
 && cd /tmp/libdecor-0.2.3 \
 && python3 /opt/meson/meson.py setup build -Ddemo=false -Dgtk=disabled \
 && ninja -C build install \
 && ldconfig

RUN cd /tmp \
 && wget https://www.libsdl.org/release/SDL2-2.32.8.tar.gz \
 && tar -xzf SDL2-2.32.8.tar.gz \
 && rm SDL2-2.32.8.tar.gz \
 && cd /tmp/SDL2-2.32.8 \
 && ./configure \
 && make -j \
 && make install

RUN cd /tmp \
 && wget https://github.com/libsdl-org/SDL/releases/download/release-3.4.12/SDL3-3.4.12.tar.gz \
 && tar -xzf SDL3-3.4.12.tar.gz \
 && rm SDL3-3.4.12.tar.gz \
 && cd /tmp/SDL3-3.4.12 \
 && cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DSDL_WAYLAND=ON -DSDL_X11=ON \
 && grep -qr "#define SDL_VIDEO_DRIVER_WAYLAND 1" build \
 && grep -qr "#define SDL_VIDEO_DRIVER_X11 1" build \
 && cmake --build build -j$(nproc) \
 && cmake --install build \
 && ldconfig
