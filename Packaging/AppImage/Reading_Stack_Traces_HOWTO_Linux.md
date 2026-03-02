## Extracting file+line information from stack traces generated in-game (Linux)

The archive contains:
- `vkquake.AppImage` : the program to run
- `vkquake.debuginfo` : debug symbols to use with addr2line
- `Reading_Stack_Trace_HOWTO_Linux.md` : this file

### Example:

================ STACK TRACE ================
0 : ./vkquake(+0x16a8a8) [0x588582e618a8]
1 : ./vkquake(+0x93267) [0x588582d8a267]
2 : ./vkquake(+0x165735) [0x588582e5c735]
3 : ./vkquake(+0x165839) [0x588582e5c839]
4 : ./vkquake(+0x1669dd) [0x588582e5d9dd]
5 : ./vkquake(+0x166ea0) [0x588582e5dea0]
6 : ./vkquake(+0x936ed) [0x588582d8a6ed]

```sh
# Using relative addresses: 
addr2line -f -e vkquake.debuginfo 0x16a8a8 0x93267 0x165735 0x165839 0x1669dd 0x166ea0 0x936ed
```
or
```sh
# Using absolute addresses: 
addr2line -f -e vkquake.debuginfo 0x588582e618a8 0x588582d8a267 0x588582e5c735 0x588582e5c839 0x588582e5d9dd 0x588582e5dea0 0x588582d8a6ed
```
Use whatever works best.
