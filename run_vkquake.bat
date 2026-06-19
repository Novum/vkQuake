@echo off
setlocal

set VKQUAKE_EXE=C:\project\vkQuake\Windows\VisualStudio\Build-vkQuake\x64\Release\vkQuake.exe
set STEAM_QUAKE_DIR=C:\Program Files (x86)\Steam\steamapps\common\Quake

if not exist "%VKQUAKE_EXE%" (
    echo Error: vkQuake.exe not found at %VKQUAKE_EXE%
    echo Build the project first with: msbuild Windows\VisualStudio\vkquake.sln /p:Configuration=Release /p:Platform=x64
    pause
    exit /b 1
)

if not exist "%STEAM_QUAKE_DIR%\id1\PAK0.PAK" (
    echo Error: Quake data not found at %STEAM_QUAKE_DIR%\id1\PAK0.PAK
    pause
    exit /b 1
)

copy /Y "%VKQUAKE_EXE%" "%STEAM_QUAKE_DIR%\vkQuake.exe" >nul
echo Copied vkQuake.exe to Steam directory.
echo Starting vkQuake...
cd /d "%STEAM_QUAKE_DIR%"
start "" vkQuake.exe %*
