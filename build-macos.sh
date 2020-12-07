#!/bin/bash

### Change dir to script-dir
cd "$(dirname "$0")"

echo -e "\nChecking:"
### Xcode / CommandLine tools 
if [ $(xcode-select -p 1>/dev/null;echo $?) -eq "0" ];then
	echo -e "Found Xcode / CommandLine Tools"
else 
	echo -e "ERROR: Didn't find Xcode / Commandline Tolls, install it first:\n"
	echo -e "sudo xcode-select --install\n"
	exit
fi

### Homebrew - things you should have installed before running this.
VKQ_CHECK_SDL2="sdl2"
if [ $(pkg-config --exists ${VKQ_CHECK_SDL2}; echo $?) -eq "0" ];then
	echo -e "Found SDL2"
else 
	echo -e "ERROR: Didn't find SDL2, insteall it first with Homebrew:\n"
	echo -e "brew install sdl2\n"
	exit
fi

VKQ_CHECK_LIBVORBIS="vorbis"
if [ $(pkg-config --exists ${VKQ_CHECK_LIBVORBIS}; echo $?) -eq "0" ];then
	echo -e "Found libvorbis"
else 
	echo -e "ERROR: Didn't find libvorbis, insteall it first with Homebrew:\n"
	echo -e "brew install libvorbis\n"
	exit
fi

VKQ_CHECK_FLAC="flac"
if [ $(pkg-config --exists ${VKQ_CHECK_FLAC}; echo $?) -eq "0" ];then
	echo -e "Found FLAC"
else 
	echo -e "ERROR: Didn't find SDL2, insteall it first with Homebrew:\n"
	echo -e "brew install libvorbis\n"
	exit
fi

VKQ_CHECK_MAD="mad"
if [ $(pkg-config --exists ${VKQ_CHECK_MAD}; echo $?) -eq "0" ];then
	echo -e "Found mad"
else 
	echo -e "ERROR: Didn't find mad, insteall it first with Homebrew:\n"
	echo -e "brew install mad\n"
	exit
fi

### Install the Vulkan SDK!
if [[ -z "${VULKAN_SDK}" ]]; then
	echo -e "\nERROR: \$VULKAN_SDK is not defined."
	echo -e ""
	echo -e "       Make sure you installed it correctly and that you"
	echo -e "       have added the setup-env.sh in your ~/.bash_profile"
	echo -e ""
	echo -e "       # Setup env's for Vulkan SDK ..."
	echo -e "       source /\${PATH-TO-VULKAN-SDK}/setup-env.sh"
	echo -e ""
	echo -e "INFO:  If you haven't installed the Vulkan SDK, might start there..."
  	echo -e "       https://vulkan.lunarg.com/sdk/home\n"
  	exit 
else
	echo -e "Found \$VULKAN_SDK = ${VULKAN_SDK}"

	### Re-compile all Shaders.
	echo -e "\nRe-compile all Shaders."
	cd Shaders/
	rm Compiled/*
	./compile.sh
	cd ..  

	####Build vkQuake
	echo -e "\nBuild vkQuake"
	cd Quake
	make
	cd ..

	if [[ -x Quake/vkquake ]]; then
		echo -e "\nPlace the Quake/vkquake file into your game dir and have fun :-)\n"
	else
		echo -e "\nSad news, the build faild :-( ...\n"
	fi
fi
