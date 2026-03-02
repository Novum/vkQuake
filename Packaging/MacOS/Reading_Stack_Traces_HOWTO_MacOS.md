## Extracting file+line information from stack traces generated in-game (MacOS)

The archive contains:
- `vkquake` : the program to run
- `vkquake.dSYM` : Directory containing debug symbols for the program
- `Reading_Stack_Trace_HOWTO_MacOS.md` : this file

### Example:

================ STACK TRACE ================
0x100003f4d 0x100003f01 0x100003ed0 0x100003ea0 0x7fff2035a3d5
0 : 0   vkQuake                 0x0000000100003f4d Sys_StackTrace + 77
1 : 1   vkQuake                 0x0000000100003f01 level3 + 17
2 : 2   vkQuake                 0x0000000100003ed0 level2 + 16
3 : 3   vkQuake                 0x0000000100003ea0 level1 + 16
4 : 4   libdyld.dylib           0x00007fff2035a3d5 start + 1

```sh
# Pass the first line :"
atos -o vkquake 0x100003f4d 0x100003f01 0x100003ed0 0x100003ea0 0x7fff2035a3d5
```

