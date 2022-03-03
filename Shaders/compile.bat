@echo off
setlocal EnableDelayedExpansion

if exist "%VS170COMNTOOLS%\VsDevCmd.bat" ( 
call "%VS170COMNTOOLS%\VsDevCmd.bat"
) else if exist "%VS160COMNTOOLS%\VsDevCmd.bat" ( 
call "%VS160COMNTOOLS%\VsDevCmd.bat"
) else if exist "%VS150COMNTOOLS%\VsDevCmd.bat" ( 
call "%VS150COMNTOOLS%\VsDevCmd.bat"
) else if exist "%VS140COMNTOOLS%\VsDevCmd.bat" ( 
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
	bintoc.exe Compiled/%%~nf.vspv %%~nf_vert_spv Compiled/%%~nf.vert.c
)

for %%f in (*.frag) do (
	%VULKAN_SDK%\bin\glslangValidator.exe -V %%f -o Compiled/%%~nf.fspv
	bintoc.exe Compiled/%%~nf.fspv %%~nf_frag_spv Compiled/%%~nf.frag.c
)

for %%f in (*.comp) do (
	set "file=%%f"
	If not "!file!"=="!file:sops=!" (
		%VULKAN_SDK%\bin\glslangValidator.exe --target-env vulkan1.1 -V %%f -o Compiled/%%~nf.cspv
	) else (
		%VULKAN_SDK%\bin\glslangValidator.exe -V %%f -o Compiled/%%~nf.cspv
	)
	bintoc.exe Compiled/%%~nf.cspv %%~nf_comp_spv Compiled/%%~nf.comp.c
)