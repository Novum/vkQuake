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

Open the Visual Studio solution, `Windows\VisualStudio\vkquake.sln`, and compile vkQuake as needed. The resulting files are
put under `Windows\VisualStudio\Build-vkQuake`. vkQuake needs `pak0.pak` that comes with your copy of Quake 1 under the `id1`
subfolder, e.g. `Windows\VisualStudio\Build-vkQuake\x64\Release\id1\pak0.pak`.

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

vkQuake needs `pak0.pak` that comes with your copy of Quake 1 under the `id1` subfolder, i.e. `Quake\id1\pak0.pak`.

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

vkQuake needs `pak0.pak` that comes with your copy of Quake 1 under the `id1` subfolder, i.e. `Quake\id1\pak0.pak`.
Then you can start the game with the `vkquake` executable:

~~~
./vkquake
~~~
