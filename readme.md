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
  * Architecure: i686
  * Install location: C:\mingw-w32
* 64 bit:
  * Architecure: x86_64
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

## Ubuntu

Make sure that both your [GPU](https://en.wikipedia.org/wiki/Vulkan_(API)#Compatibility) and your GPU driver supports Vulkan.

To compile vkQuake, first install the build dependencies:

~~~
apt install git make gcc libsdl2-dev libvulkan-dev libvorbis-dev libmad0-dev
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
[shareware version of Quake](http://bit.ly/2aDMSiz). Use [7-Zip](http://7-zip.org/) or a similar file archiver to extract
`quake106.zip/resource.1/ID1/PAK0.PAK`. Alternatively, if you own the game, you can obtain both .pak files from its install media.

Now locate your vkQuake executable, i.e. `vkQuake.exe` on Windows or `vkquake` on Ubuntu. You need to create an `id1` directory
next to that and copy `pak0.pak` there, e.g.:

* Windows: `Windows\VisualStudio\Build-vkQuake\x64\Release\id1\pak0.pak`
* Ubuntu: `Quake\id1\pak0.pak`

Then vkQuake is ready to play.
