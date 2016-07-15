@echo off
%VULKAN_SDK%\bin\glslangValidator.exe -V basic.vert -o basic.vspv
%VULKAN_SDK%\bin\glslangValidator.exe -V basic.frag -o basic.fspv
pause