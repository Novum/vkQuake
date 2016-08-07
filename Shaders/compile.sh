#!/bin/bash

#set your VULKAN_SDK location  before running
VULKAN_SDK=~/VulkanSDK/1.0.21.1/x86_64/
if [[ ! -x "./bintoc" ]]
then
	gcc bintoc.c -o bintoc
fi

find -type f -name "*.vert" | \
	while read f; do $VULKAN_SDK/bin/glslangValidator -V ${f} -o "Compiled/${f%.*}.vspv"; done

find -type f -name "*.frag" | \
	while read f; do $VULKAN_SDK/bin/glslangValidator -V ${f} -o "Compiled/${f%.*}.fspv"; done


find -type f -name "*.vspv" | \
	while read f; do ./bintoc ${f} `basename ${f%.*}`_vert_spv > ${f%.*}_vert.c; done

find -type f -name "*.fspv" | \
	while read f; do ./bintoc ${f} `basename ${f%.*}`_frag_spv > ${f%.*}_frag.c; done
