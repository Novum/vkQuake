$BaseVersionMatch=Select-String "^#define\s*VKQUAKE_VERSION\s*([0-9.]*)" "../../Quake/quakedef.h"
$BaseVersion=$BaseVersionMatch.Matches.groups[1].value
$PatchVersionMatch=Select-String "^#define\s*VKQUAKE_VER_PATCH\s*([0-9.]*)" "../../Quake/quakedef.h"
$PatchVersion=$PatchVersionMatch.Matches.groups[1].value
$SuffixMatch=Select-String "^#define\s*VKQUAKE_VER_SUFFIX\s*`"([^`"]*)" "../../Quake/quakedef.h"
$Suffix=$SuffixMatch.Matches.groups[1].value
$Version=$BaseVersion + '.' + $PatchVersion + $Suffix
$SrcDirX64="..\..\Windows\VisualStudio\Build-vkQuake\x64\Release"
$SrcDirX86="..\..\Windows\VisualStudio\Build-vkQuake\x86\Release"

# Cleanup
Del "vkQuake-*.exe"
Del "vkQuake-*.zip"

# Clean & build binaries
msbuild ..\..\Windows\VisualStudio\vkquake.sln /target:Clean /target:Build /property:Configuration=Release /property:Platform=x64
msbuild ..\..\Windows\VisualStudio\vkquake.sln /target:Clean /target:Build /property:Configuration=Release /property:Platform=Win32

# Create NSIS exe installers
$Nsis="C:\Program Files (x86)\NSIS\Bin\makensis.exe"
$NsisArguments = '-V4 -DSRCDIR="' + $SrcDirX64 + '" -DPLATFORM=x64 -DVERSION=' + $Version + ' vkQuake.nsi'
Start-Process -Wait -NoNewWindow -PassThru -FilePath $Nsis -ArgumentList $NsisArguments
$NsisArguments = '-V4 -DSRCDIR="' + $SrcDirX86 + '" -DPLATFORM=x86 -DVERSION=' + $Version + ' vkQuake.nsi'
Start-Process -Wait -NoNewWindow -PassThru -FilePath $Nsis -ArgumentList $NsisArguments

# Create zip files
$compress = @{
  Path = "$SrcDirX64\*.exe", "$SrcDirX64\*.dll", "..\..\LICENSE.txt"
  CompressionLevel = "Optimal"
  DestinationPath = "vkQuake-" + $Version + "_win64.zip"
}
Compress-Archive @compress
$compress = @{
  Path = "$SrcDirX86\*.exe", "$SrcDirX86\*.dll", "..\..\LICENSE.txt"
  CompressionLevel = "Optimal"
  DestinationPath = "vkQuake-" + $Version + "_win32.zip"
}
Compress-Archive @compress