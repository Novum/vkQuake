# 🌋 vkQuake
vkQuake is a Quake 1 port using Vulkan instead of OpenGL for rendering. It is based on the popular [QuakeSpasm](http://quakespasm.sourceforge.net/) and [QuakeSpasm-Spiked](https://triptohell.info/moodles/qss/) ports and runs all mods compatible with it like [Arcane Dimensions](http://www.simonoc.com/pages/design/sp/ad.htm). Due to the port using Vulkan and other optimizations it can achieve much better frame rates.

Compared to QuakeSpasm vkQuake also features a software Quake like underwater effect, has better color precision, generates mipmap for water surfaces at runtime and has native support for anti-aliasing and AF. Furthermore frame rates above 72FPS do not break physics.

vkQuake also serves as a Vulkan demo application that shows basic usage of the API. For example it demonstrates render passes & sub passes, pipeline barriers & synchronization, compute shaders, push & specialization constants, CPU/GPU parallelism and memory pooling.

# Installation

## Windows
Copy all files from the `vkquake-<version>` folder in the zip file to the Quake base directory. Overwrite any existing files. Afterward to run the game just execute `vkQuake.exe`.

# Building

## Windows

Prerequisites:

* [Git for Windows](https://github.com/git-for-windows/git/releases)
* [Vulkan SDK](https://vulkan.lunarg.com/signin) (download link is on the bottom of page)
* A [Vulkan-capable GPU](https://en.wikipedia.org/wiki/Vulkan_(API)#Compatibility) with the appropriate drivers installed

Start `Git Bash` and clone the vkQuake repo:

~~~
git clone https://github.com/Novum/vkQuake.git
~~~

### Visual Studio

Install [Visual Studio Community](https://www.visualstudio.com/products/free-developer-offers-vs) with Visual C++ component.

Open the Visual Studio solution, `Windows\VisualStudio\vkquake.sln`, select the desired configuration and platform, then
build the solution.

### MinGW

Download the latest release of [MinGW-w64](https://sourceforge.net/projects/mingw-w64/files/latest/download) and install it:

* 32 bit:
  * Architecture: i686
  * Install location: C:\mingw-w32
* 64 bit:
  * Architecture: x86_64
  * Install location: C:\mingw-w64

Also install the latest release of
[MozillaBuild](https://ftp.mozilla.org/pub/mozilla.org/mozilla/libraries/win32/MozillaBuildSetup-Latest.exe) with default settings.

Start MSYS with `c:\mozilla-build\msys\msys.bat` and compile vkQuake.

32 bit:

~~~
cd vkQuake/Quake
export PATH=${PATH}:/c/mingw-w32/mingw32/bin
make USE_SDL2=1 -f Makefile.w32
~~~

64 bit:

~~~
cd vkQuake/Quake
export PATH=${PATH}:/c/mingw-w64/mingw64/bin
make USE_SDL2=1 -f Makefile.w64
~~~

## Linux

Make sure that both your [GPU](https://en.wikipedia.org/wiki/Vulkan_(API)#Compatibility) and your GPU driver supports Vulkan.

To compile vkQuake, first install the build dependencies:

Ubuntu:
~~~
apt-get install git make gcc libsdl2-dev libvulkan-dev libvorbis-dev libmad0-dev libx11-xcb-dev
~~~

Arch Linux:
~~~
pacman -S git flac glibc libgl libmad libvorbis libx11 sdl2 vulkan-validation-layers
~~~

\* Please note that for vkquake > v0.50, you will need at least v1.0.12.0 of libvulkan-dev (See [#55](https://github.com/Novum/vkQuake/issues/55)).

Then clone the vkQuake repo:

~~~
git clone https://github.com/Novum/vkQuake.git
~~~

Now go to the Quake directory and compile the executable:

~~~
cd vkQuake/Quake
make
~~~

### Note
vkQuake 0.97 and later requires at least **SDL2 2.0.6 with enabled Vulkan support**. The precompiled versions in some of the distribution repositories (e.g. Ubuntu) do not currently ship with Vulkan support. You will therefore need to compile it from source. Make sure you have libvulkan-dev installed before running configure.

## MacOS

To compile vkQuake, first install the build dependencies with Homebrew:

~~~
brew install molten-vk vulkan-headers sdl2 libvorbis flac mad
~~~

Then clone the vkQuake repo:

~~~
git clone https://github.com/Novum/vkQuake.git
~~~

Now go to the Quake directory and compile the executable:

~~~
cd vkQuake/Quake
make
~~~

# Usage

Quake has 4 episodes that are split into 2 files:

* `pak0.pak`: contains episode 1
* `pak1.pak`: contains episodes 2-4

These files aren't free to distribute, but `pak0.pak` is sufficient to run the game and it's freely available via the
[shareware version of Quake](https://ftp.gwdg.de/pub/misc/ftp.idsoftware.com/idstuff/quake/). Use [7-Zip](http://7-zip.org/) or a similar file archiver to extract
`quake106.zip/resource.1/ID1/PAK0.PAK`. Alternatively, if you own the game, you can obtain both .pak files from its install media.

Now locate your vkQuake executable, i.e. `vkQuake.exe` on Windows or `vkquake` on Ubuntu. You need to create an `id1` directory
next to that and copy `pak0.pak` there, e.g.:

* Windows: `Windows\VisualStudio\Build-vkQuake\x64\Release\id1\pak0.pak`
* Ubuntu: `Quake\id1\pak0.pak`

Then vkQuake is ready to play.

# Optional - Music / Soundtrack

The original quake had a great soundtrack by Nine Inch Nails. Unfortunately, the Steam version does not come with the soundtrack files. The GOG-provided files need to be converted before they are ready for use. In general, you'll just need to move a "music" folder to the correct location within your vkQuake installation (.e.g `/usr/share/quake/id1/music`). Most Quake engines play nicest with soundtracks placed in the `id1/music` subfolder vs. `sound\cdtracks`

QuakeSpasm, the engine vkQuake is derived from, supports OGG, MP3, FLAC, and WAV audio formats. The Linux version of QuakeSpasm/VkQuake requires external libraries: libogg or libvorbis for OGG support, libmad or libmpg123 for MP3, and libflac for FLAC. If you already have a setup that works for the engine you're currently using, then you don't necessarily have to change it. 

To convert the WAV files to FLAC, run this command with libflac and sox installed in a directory containing the WAV files:
~~~
for f in *.wav; do sox "$f" "${f%.wav}.flac"; done
~~~

Generally, the below setup works for multiple engines, including Quakespasm/vkQuake:

* The music files are loose files, NOT inside a pak or pk3 archive.
* The files are placed inside a "music" subfolder of the "id1" folder. For missionpack or mod soundtracks, the files are placed in a "music" subfolder of the appropriate game folder. So the original Quake soundtrack files go inside "id1\music", Mission Pack 1 soundtrack files go inside "hipnotic\music", and Mission Pack 2 soundtrack files go inside "rogue\music".
* The files are named in the pattern "tracknn", where "nn" is the CD track number that the file was ripped from. Since the soundtrack starts at the second CD track, MP3 soundtrack files are named "track02.mp3", "track03.mp3", etc. OGG soundtrack files are named "track02.ogg", "track03.ogg", etc. FLAC soundtrack files are named "track02.flac", "track03.flac", etc. WAV soundtrack files are named "track02.wav", "track03.wav", etc.

**See more:** [Quake Soundtrack Solutions (Steam Community)](http://steamcommunity.com/sharedfiles/filedetails/?id=119489135)
