# 🌋 vkQuake
[![Windows CI](https://github.com/Novum/vkQuake/actions/workflows/build-windows.yml/badge.svg)](https://github.com/Novum/vkQuake/actions/workflows/build-windows.yml) [![Windows CI](https://github.com/Novum/vkQuake/actions/workflows/build-mingw.yml/badge.svg)](https://github.com/Novum/vkQuake/actions/workflows/build-mingw.yml) [![Linux CI](https://github.com/Novum/vkQuake/actions/workflows/build-linux.yml/badge.svg)](https://github.com/Novum/vkQuake/actions/workflows/build-linux.yml) [![macOS CI](https://github.com/Novum/vkQuake/actions/workflows/build-mac.yml/badge.svg)](https://github.com/Novum/vkQuake/actions/workflows/build-mac.yml) [![Formatting](https://github.com/Novum/vkQuake/actions/workflows/clang-format-check.yml/badge.svg)](https://github.com/Novum/vkQuake/actions/workflows/clang-format-check.yml)

vkQuake is a port of id Software's [Quake](https://en.wikipedia.org/wiki/Quake_(video_game)) using Vulkan instead of OpenGL for rendering. It is based on the popular [QuakeSpasm](http://quakespasm.sourceforge.net/) and [QuakeSpasm-Spiked](https://triptohell.info/moodles/qss/) ports and runs all mods compatible with QuakeSpasm like [Arcane Dimensions](http://www.moddb.com/mods/arcane-dimensions). 

Improvements over QuakeSpasm include:
* Much better performance with multithreaded rendering and loading
* The game can run at higher frame rates than 72Hz without breaking physics
* A software Quake like underwater effect
* Support for remastered models if using data from the 2021 rerelease
* Dynamic shadows (requires a GPU with ray tracing support)
* Better color precision reducing banding in dark areas
* Native support for anti aliasing and anisotropic filtering
* 8-bit color emulation
* Scaling for pixelated look
* Mods menu for easy mod loading
* More modern protocol to avoid certain movement issues (from QSS)
* Support for custom mod HUDs (from QSS)
* Support for scriptable particles (from QSS)

# Installation

## Windows
It is recommended to use the installer on Windows. This sets up start menu entries for the original and remastered Quake versions. Save data and config files will be written to the user folder (`%APPDATA\vkQuake`) instead of the Quake data folder.

Otherwise copy all files inside the `vkquake-<version>_win64` or `vkquake-<version>_win32` folder in the zip to the Quake base directory. Overwrite any existing files. Afterward to run the game just execute `vkQuake.exe`.

## Linux
Copy all files inside the `vkquake-<version>-linux64` folder in the tar archive to the Quake base directory. Overwrite any existing files. Run `vkquake.AppImage`.

> **Note**\
> Make sure all data files are lowercase, e.g. "id1", not "ID1" and "pak0.pak", not "PAK0.PAK". Some distributions of the game have upper case file names, e.g. from GOG.com.

## OpenBSD

[OpenBSD](https://openbsd.org) includes vkQuake in the standard package repositories since version [6.6](https://www.openbsd.org/66.html).

If you're running `OpenBSD 6.6` or greater you can install the package with:

```console
$ pkg_add vkquake
```

## Quake '2021 re-release'

vkQuake has initial support for playing the 2021 re-release content. Follow installation instructions as above but copy the files into the rerelease folder.

# Vulkan
vkQuake shows basic usage of the API. For example it demonstrates render passes & sub passes, pipeline barriers & synchronization, compute shaders, push & specialization constants, CPU/GPU parallelism and memory pooling.

# Building
> **Note**\
> You will need at least Vulkan SDK version 1.2.162 or newer. When building for Linux this is not always the case for the SDK provided by the distribution. Install the latest LunarG SDK if necessary.

## Windows

Clone the vkQuake repo from `https://github.com/Novum/vkQuake.git`

Prerequisites:

* [Git for Windows](https://github.com/git-for-windows/git/releases)
* A [Vulkan-capable GPU](https://en.wikipedia.org/wiki/Vulkan_(API)#Compatibility) with the appropriate drivers installed
* Install the latest [Vulkan SDK](https://vulkan.lunarg.com/sdk/home). Log out and back in after installation to make sure environment variables are set.

### Visual Studio

* Install [Visual Studio Community](https://www.visualstudio.com/products/free-developer-offers-vs) with Visual C++ component.

Open the Visual Studio solution, `Windows\VisualStudio\vkquake.sln`, select the desired configuration and platform, then
build the solution.

## Linux

Make sure that both your GPU and your GPU driver support [Vulkan](https://en.wikipedia.org/wiki/Vulkan#Support_across_vendors).

To compile vkQuake, first install the build dependencies:

Ubuntu:
~~~
apt-get install git meson gcc glslang-tools spirv-tools libsdl2-dev libvulkan-dev libvorbis-dev libmad0-dev libx11-xcb-dev
~~~

Arch Linux:
~~~
pacman -S git meson flac glibc libgl libmad libvorbis libx11 sdl2 vulkan-headers glslang spirv-tools
~~~

Then clone the vkQuake repo:

~~~
git clone https://github.com/Novum/vkQuake.git
~~~

Now go to the Quake directory and compile the executable:

~~~
cd vkQuake
meson build && ninja -C build
~~~

> **Note**\
> The Meson version needs to be 0.47.0 or newer. For older distributions you can use make:
> ~~~
> cd vkQuake/Quake
> make -j
> ~~~
> Meson is the preferred way to build vkQuake because it automatically checks for out of date file depenencies, is faster and has better error reporting for missing dependencies.

> **Note**\
> vkQuake 0.97 and later requires at least **SDL2 2.0.6 with enabled Vulkan support**. The precompiled versions in some of the distribution repositories (e.g. Ubuntu) do not currently ship with Vulkan support. You will therefore need to compile it from source. Make sure you have libvulkan-dev installed before running configure.

## MacOS

To compile vkQuake, first install the build dependencies with Homebrew:

~~~
brew install molten-vk vulkan-headers glslang spirv-tools sdl2 libvorbis flac opus opusfile flac mad meson pkgconfig
~~~

Then clone the vkQuake repo:

~~~
git clone https://github.com/Novum/vkQuake.git
~~~

Now go to the Quake directory and compile the executable:

~~~
cd vkQuake
meson build && ninja -C build
~~~

> **Note**\
> The Meson version needs to be 0.47.0 or newer.

### MinGW

Setup your [MinGW-w64](https://sourceforge.net/projects/mingw-w64/) environment, e.g. using [w64devkit](https://github.com/skeeto/w64devkit) or [MSYS2](https://www.msys2.org/).

Build 32 bit (x86) vkQuake:

~~~
cd vkQuake/Quake
make -f Makefile.w32
~~~

Build 64 bit (x64) vkQuake:

~~~
cd vkQuake/Quake
make -f Makefile.w64
~~~

If you are on Linux and want to cross-compile for Windows, see the `build_cross_win??.sh` scripts.

# Optional - Music / Soundtrack

> **Note**\
> This section only applies to older releases. For the 2021 re-release music will work out of the box.

The original Quake had a great soundtrack by Nine Inch Nails. Unfortunately, the Steam version does not come with the soundtrack files. The GOG-provided files need to be converted before they are ready for use. In general, you'll just need to move a "music" folder to the correct location within your vkQuake installation (.e.g `/usr/share/quake/id1/music`). Most Quake engines play nicest with soundtracks placed in the `id1/music` subfolder vs. `sound\cdtracks`

QuakeSpasm, the engine vkQuake is derived from, supports OGG, MP3, FLAC, and WAV audio formats. The Linux version of QuakeSpasm/VkQuake requires external libraries: libogg or libvorbis for OGG support, libmad or libmpg123 for MP3, and libflac for FLAC. If you already have a setup that works for the engine you're currently using, then you don't necessarily have to change it. 

Generally, the below setup works for multiple engines, including Quakespasm/vkQuake:

* The music files are loose files, NOT inside a pak or pk3 archive.
* The files are placed inside a "music" subfolder of the "id1" folder. For missionpack or mod soundtracks, the files are placed in a "music" subfolder of the appropriate game folder. So the original Quake soundtrack files go inside "id1\music", Mission Pack 1 soundtrack files go inside "hipnotic\music", and Mission Pack 2 soundtrack files go inside "rogue\music".
* The files are named in the pattern "tracknn", where "nn" is the CD track number that the file was ripped from. Since the soundtrack starts at the second CD track, MP3 soundtrack files are named "track02.mp3", "track03.mp3", etc. OGG soundtrack files are named "track02.ogg", "track03.ogg", etc. FLAC soundtrack files are named "track02.flac", "track03.flac", etc. WAV soundtrack files are named "track02.wav", "track03.wav", etc.

**See more:** [Quake Soundtrack Solutions (Steam Community)](http://steamcommunity.com/sharedfiles/filedetails/?id=119489135)
