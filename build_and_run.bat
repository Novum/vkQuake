@echo off
setlocal

echo === Building vkQuake (Release x64) ===
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
msbuild "Windows\VisualStudio\vkquake.sln" /p:Configuration=Release /p:Platform=x64 /t:vkquake /m /verbosity:minimal
if errorlevel 1 (
    echo Build failed!
    pause
    exit /b 1
)

echo === Build succeeded ===
call run_vkquake.bat %*
