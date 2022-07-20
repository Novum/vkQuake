#!/bin/zsh

# For now, just compile for release as the debug shaders were fine.
# Can expand this later or someone with more than a rudimentry knoweldge
# of shell scripts can fix this!

# VULKAN_SDK should either be set or by default, the SDK will install into /usr/local/bin
# so if installed properly, glslangValidator SHOULD already be in the path

# Just compile bintoc anyways
gcc bintoc.c -o bintoc

find . -type f -name "*.vert" | while read f;
do
	glslangValidator -V ${f} -o "Compiled/Release/${f%.*}.vspv";
done

find . -type f -name "*.frag" | while read f;
do
  glslangValidator -V ${f} -o "Compiled/Release/${f%.*}.fspv";
done

find . -type f -name "*.comp" | while read f; do filename=${f} substring="sops"
if test "${filename#*$substring}" != "$filename";
then glslangValidator -V ${f} --target-env vulkan1.1 -o "Compiled/Release/${f%.*}.cspv";
else glslangValidator -V ${f} -o "Compiled/Release/${f%.*}.cspv";
fi
done

find . -type f -name "*.vspv" | while read f;
do
        ./bintoc ${f} $(basename ${f%.*}_vert_spv) ${f%.*}.vert.c;
        rm ${f}; # Cleanup .vspv
done

find . -type f -name "*.fspv" | while read f;
do
        ./bintoc ${f} $(basename ${f%.*}_frag_spv) ${f%.*}.frag.c;
        rm ${f}; # Cleanup .fspv
done

find . -type f -name "*.cspv" | while read f;
do
    ./bintoc ${f} $(basename ${f%.*}_comp_spv) ${f%.*}.comp.c;
    rm ${f}; # Cleanup .cspv
done
