# Error reporting

In order to get the most context for fatal errors we need to have Debug information correcty installed 
either alongside the `vkQuake` executable, or in a place where it can be found for post-mortem analysis:

-  Windows MSVC : Debug information is in the `vkQuake.pdb` file:
  
    - Using `vkQuake-Installer-windows_x64-N.NN.N.exe` : Automatically installs the `.pdb` file at the right place.
    - Using `vkQuake-N.NN.N_windows_x64.zip` : put the `vkQuake.pdb` file at the same place as the `vkQuake.exe` file.
 
-  Windows MSYS2 for either `x64` or `arm64` : Debug information is already built-in the `vkQuake.exe` file.  

- Linux :
   - Using the Appimage build: the archive contains `vkquake.debuginfo` that will be used for post-mortem analysis. 
   - Using either `Meson` or `Makefile` builds : Debug information is already built-in the `vkquake` executable.
 
- MacOS :
   - `vkquake.dSYM` directory contains the Debug information that will be used for post-mortem analysis, and should be put at the same place
     where the `vkquake` exeutable is. 
 
## Extracting file+line information from in-game vkQuake fatal errors :  

### _Examples:_

### Windows MSVC

Error reporting is directly built-in in the game : Users can report screenshots of either `Host_Error` console errors
or `QUAKE ERROR` dialogs. 

### Linux, MacOS, or Windows MSYS2 (x64 or ARM64)

`Host_Error` and `Quake Error` dialogs only report raw stack traces with minimal context,
often limited to function names in the best of cases. 
In order to get file+line information Users will need to run some external tools.  

-  #### MSYS2:

```c
STACK TRACE
0x100003f4d 0x100003f01 0x100003ed0 0x100003ea0 0x7fff2035a3d5
```
Run: 
```sh
addr2line -e vkquake 0x16a8a8 0x93267 0x165735 0x165839 0x1669dd 0x166ea0 0x936ed
```


-  #### Linux:
 
```c
STACK TRACE
0 : ./vkquake(+0x16a8a8) [0x588582e618a8]
1 : ./vkquake(+0x93267) [0x588582d8a267]
2 : ./vkquake(+0x165735) [0x588582e5c735]
3 : ./vkquake(+0x165839) [0x588582e5c839]
4 : ./vkquake(+0x1669dd) [0x588582e5d9dd]
5 : ./vkquake(+0x166ea0) [0x588582e5dea0]
6 : ./vkquake(+0x936ed) [0x588582d8a6ed]
```
Run:
```sh
# Using relative addresses: 
addr2line -f -e [VKQUAKEBEBUG] 0x16a8a8 0x93267 0x165735 0x165839 0x1669dd 0x166ea0 0x936ed
```
or Run:
```sh
# Using absolute addresses: 
addr2line -f -e [VKQUAKEBEBUG] 0x588582e618a8 0x588582d8a267 0x588582e5c735 0x588582e5c839 0x588582e5d9dd 0x588582e5dea0 0x588582d8a6ed
```
Use whatever works best, where `[VKQUAKEBEBUG]` is the vkQuake executable if built with either `Meson` or `Makefile`, or `vkquake.debuginfo` 
for Appimage.  


-  ##### MacOS:

```c
STACK TRACE
0x100003f4d 0x100003f01 0x100003ed0 0x100003ea0 0x7fff2035a3d5
0 : 0   vkquake                 0x0000000100003f4d Sys_StackTrace + 77
1 : 1   vkquake                 0x0000000100003f01 level3 + 17
2 : 2   vkquake                 0x0000000100003ed0 level2 + 16
3 : 3   vkquake                 0x0000000100003ea0 level1 + 16
4 : 4   libdyld.dylib           0x00007fff2035a3d5 start + 1
```
Run:
```sh
# Pass the first line :
atos -o vkquake 0x100003f4d 0x100003f01 0x100003ed0 0x100003ea0 0x7fff2035a3d5
```

----------------------------

As a result of running either `addr2line` or `atos` we get file+line trace information looking like this :
```c
sys_sdl_win.c:461
sys_sdl_win.c:278
wad.c:82
host.c:1115
main_sdl.c:112
SDL_windows_main.c:?

```
That can be reported to `vkQuake` maintainers.
