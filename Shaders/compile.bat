@echo off
if exist "%VS140COMNTOOLS%\VsDevCmd.bat" ( 
call "%VS140COMNTOOLS%\VsDevCmd.bat"
) else if exist "%VS120COMNTOOLS%\VsDevCmd.bat" ( 
call "%VS120COMNTOOLS%\VsDevCmd.bat"
) else if exist "%VS110COMNTOOLS%\VsDevCmd.bat" ( 
call "%VS110COMNTOOLS%\VsDevCmd.bat"
)
cl.exe /nologo bintoc.c 

%VULKAN_SDK%\bin\glslangValidator.exe -V basic.vert -o Compiled/basic.vspv
%VULKAN_SDK%\bin\glslangValidator.exe -V basic.frag -o Compiled/basic.fspv
%VULKAN_SDK%\bin\glslangValidator.exe -V basic_notex.frag -o Compiled/basic_notex.fspv
%VULKAN_SDK%\bin\glslangValidator.exe -V world.vert -o Compiled/world.vspv
%VULKAN_SDK%\bin\glslangValidator.exe -V world.frag -o Compiled/world.fspv

bintoc.exe Compiled/basic.vspv basic_vert_spv > Compiled/basic_vert.c
bintoc.exe Compiled/basic.fspv basic_frag_spv > Compiled/basic_frag.c
bintoc.exe Compiled/basic_notex.fspv basic_notex_frag_spv > Compiled/basic_notex_frag.c
bintoc.exe Compiled/world.vspv world_vert_spv > Compiled/world_vert.c
bintoc.exe Compiled/world.fspv world_frag_spv > Compiled/world_frag.c

pause