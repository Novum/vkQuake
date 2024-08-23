#!/bin/sh
BASE_VERSION=`grep -oP "^#define\s*VKQUAKE_VERSION\s*\K[0-9.]*" ../../Quake/quakever.h`
PATCH_VERSION=`grep -oP "^#define\s*VKQUAKE_VER_PATCH\s*\K[0-9.]*" ../../Quake/quakever.h`
SUFFIX=`grep -oP "^#define\s*VKQUAKE_VER_SUFFIX\s*\"\K([^\"]*)" ../../Quake/quakever.h`
echo ${BASE_VERSION}.${PATCH_VERSION}${SUFFIX}
