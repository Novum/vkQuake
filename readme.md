# Building

## Windows

Prerequisites:

* [Git for Windows](https://github.com/git-for-windows/git/releases)
* [Visual Studio Community](https://www.visualstudio.com/products/free-developer-offers-vs) with Visual C++ component
* [Vulkan SDK](https://vulkan.lunarg.com/signin) (download link is on the bottom of page)
* A [Vulkan-capable GPU](https://en.wikipedia.org/wiki/Vulkan_(API)#Compatibility) with the appropriate drivers installed

Start `Git Bash` and clone the vkQuake repo:

~~~
git clone https://github.com/Novum/vkQuake.git
~~~

Open the Visual Studio solution, `Windows\VisualStudio\vkquake.sln`, and compile vkQuake as needed. The resulting files are
put under `Windows\VisualStudio\Build-vkQuake`. vkQuake needs `pak0.pak` that comes with your copy of Quake 1 under the `id1`
subfolder, e.g. `Windows\VisualStudio\Build-vkQuake\x64\Release\id1\pak0.pak`.
