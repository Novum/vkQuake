@echo off
msbuild ..\..\Windows\VisualStudio\vkquake.sln /target:Clean /target:Build /property:Configuration=Release /property:Platform=x64
msbuild ..\..\Windows\VisualStudio\vkquake.sln /target:Clean /target:Build /property:Configuration=Release /property:Platform=Win32
set SRCDIR=..\..\Windows\VisualStudio\Build-vkQuake\x64\Release
"C:\Program Files (x86)\NSIS\Bin\makensis.exe" -V4 -DSRCDIR=%SRCDIR% -DPLATFORM=x64 vkQuake.nsi
set SRCDIR=..\..\Windows\VisualStudio\Build-vkQuake\x86\Release
"C:\Program Files (x86)\NSIS\Bin\makensis.exe" -V4 -DSRCDIR=%SRCDIR% -DPLATFORM=x86 vkQuake.nsi
