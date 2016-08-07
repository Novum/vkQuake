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

for %%f in (*.vert) do (
	%VULKAN_SDK%\bin\glslangValidator.exe -V %%f -o Compiled/%%~nf.vspv
	bintoc.exe Compiled/%%~nf.vspv %%~nf_vert_spv > Compiled/%%~nf_vert.c
)

for %%f in (*.frag) do (
	%VULKAN_SDK%\bin\glslangValidator.exe -V %%f -o Compiled/%%~nf.fspv
	bintoc.exe Compiled/%%~nf.fspv %%~nf_frag_spv > Compiled/%%~nf_frag.c
)