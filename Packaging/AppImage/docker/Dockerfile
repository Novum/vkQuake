FROM ubuntu:22.04
ARG DEBIAN_FRONTEND=noninteractive

ARG VULKAN_SDK_VER=1.4.341.1
ARG MESON_VER=1.3.1
ARG LIBDECOR_VER=0.2.3
ARG SDL3_VER=3.4.12

RUN apt-get update \
 && apt-get install -y \
	ca-certificates \
	wget \
	build-essential \
	cmake \
	file \
	fuse \
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
	libsdl2-dev \
	libsndio-dev \
	libudev-dev \
	libusb-dev \
	libvorbis-dev \
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
	wayland-protocols \
	zlib1g-dev \
 && rm -rf /var/lib/apt/lists/*

# only the parts of the Vulkan SDK the build uses: shader tools, headers
# and the loader for linking
RUN cd /tmp \
 && wget -O vulkan-sdk.tar.xz https://sdk.lunarg.com/sdk/download/${VULKAN_SDK_VER}/linux/vulkansdk-linux-x86_64-${VULKAN_SDK_VER}.tar.xz \
 && mkdir -p /opt/vulkan-sdk \
 && tar -xf vulkan-sdk.tar.xz -C /opt/vulkan-sdk --strip-components=1 --wildcards \
	${VULKAN_SDK_VER}/x86_64/bin/glslang \
	${VULKAN_SDK_VER}/x86_64/bin/glslangValidator \
	${VULKAN_SDK_VER}/x86_64/bin/spirv-opt \
	${VULKAN_SDK_VER}/x86_64/include \
	"${VULKAN_SDK_VER}/x86_64/lib/libvulkan.so*" \
 && rm vulkan-sdk.tar.xz

ENV VULKAN_SDK=/opt/vulkan-sdk/x86_64
ENV PATH=${VULKAN_SDK}/bin:${PATH}
ENV LD_LIBRARY_PATH=${VULKAN_SDK}/lib

RUN cd /opt \
 && wget https://github.com/mesonbuild/meson/releases/download/${MESON_VER}/meson-${MESON_VER}.tar.gz \
 && tar -xzf meson-${MESON_VER}.tar.gz \
 && rm meson-${MESON_VER}.tar.gz \
 && mv meson-${MESON_VER} meson

# headers for SDL's dynamically loaded libdecor support (window
# decorations on GNOME); jammy's libdecor is older than the 0.2.0 SDL
# needs for dynamic loading
RUN cd /tmp \
 && wget https://gitlab.freedesktop.org/libdecor/libdecor/-/archive/${LIBDECOR_VER}/libdecor-${LIBDECOR_VER}.tar.gz \
 && tar -xzf libdecor-${LIBDECOR_VER}.tar.gz \
 && rm libdecor-${LIBDECOR_VER}.tar.gz \
 && cd /tmp/libdecor-${LIBDECOR_VER} \
 && python3 /opt/meson/meson.py setup build -Ddemo=false -Dgtk=disabled \
 && ninja -C build install \
 && ldconfig

RUN cd /tmp \
 && wget https://github.com/libsdl-org/SDL/releases/download/release-${SDL3_VER}/SDL3-${SDL3_VER}.tar.gz \
 && tar -xzf SDL3-${SDL3_VER}.tar.gz \
 && rm SDL3-${SDL3_VER}.tar.gz \
 && cd /tmp/SDL3-${SDL3_VER} \
 && cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DSDL_WAYLAND=ON -DSDL_X11=ON \
 && grep -qr "#define SDL_VIDEO_DRIVER_WAYLAND 1" build \
 && grep -qr "#define SDL_VIDEO_DRIVER_X11 1" build \
 && cmake --build build -j$(nproc) \
 && cmake --install build \
 && ldconfig
