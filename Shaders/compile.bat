@echo off
if exist "%VS140COMNTOOLS%\VsDevCmd.bat" ( 
call "%VS140COMNTOOLS%\VsDevCmd.bat"
) else if exist "%VS120COMNTOOLS%\VsDevCmd.bat" ( 
call "%VS120COMNTOOLS%\VsDevCmd.bat"
) else if exist "%VS110COMNTOOLS%\VsDevCmd.bat" ( 
call "%VS110COMNTOOLS%\VsDevCmd.bat"
)

if not exist bintoc.exe ( 
cl.exe /nologo bintoc.c 
)

%VULKAN_SDK%\bin\glslangValidator.exe -V basic.vert -o Compiled/basic.vspv
%VULKAN_SDK%\bin\glslangValidator.exe -V basic.frag -o Compiled/basic.fspv
%VULKAN_SDK%\bin\glslangValidator.exe -V basic_alphatest.frag -o Compiled/basic_alphatest.fspv
%VULKAN_SDK%\bin\glslangValidator.exe -V basic_notex.frag -o Compiled/basic_notex.fspv
%VULKAN_SDK%\bin\glslangValidator.exe -V world.vert -o Compiled/world.vspv
%VULKAN_SDK%\bin\glslangValidator.exe -V world.frag -o Compiled/world.fspv
%VULKAN_SDK%\bin\glslangValidator.exe -V world_fullbright.frag -o Compiled/world_fullbright.fspv
%VULKAN_SDK%\bin\glslangValidator.exe -V alias.vert -o Compiled/alias.vspv
%VULKAN_SDK%\bin\glslangValidator.exe -V alias.frag -o Compiled/alias.fspv
%VULKAN_SDK%\bin\glslangValidator.exe -V sky_layer.vert -o Compiled/sky_layer.vspv
%VULKAN_SDK%\bin\glslangValidator.exe -V sky_layer.frag -o Compiled/sky_layer.fspv

bintoc.exe Compiled/basic.vspv basic_vert_spv > Compiled/basic_vert.c
bintoc.exe Compiled/basic.fspv basic_frag_spv > Compiled/basic_frag.c
bintoc.exe Compiled/basic_notex.fspv basic_notex_frag_spv > Compiled/basic_notex_frag.c
bintoc.exe Compiled/basic_alphatest.fspv basic_alphatest_frag_spv > Compiled/basic_alphatest_frag.c
bintoc.exe Compiled/world.vspv world_vert_spv > Compiled/world_vert.c
bintoc.exe Compiled/world.fspv world_frag_spv > Compiled/world_frag.c
bintoc.exe Compiled/world_fullbright.fspv world_fullbright_frag_spv > Compiled/world_fullbright_frag.c
bintoc.exe Compiled/alias.vspv alias_vert_spv > Compiled/alias_vert.c
bintoc.exe Compiled/alias.fspv alias_frag_spv > Compiled/alias_frag.c
bintoc.exe Compiled/sky_layer.vspv sky_layer_vert_spv > Compiled/sky_layer_vert.c
bintoc.exe Compiled/sky_layer.fspv sky_layer_frag_spv > Compiled/sky_layer_frag.c
