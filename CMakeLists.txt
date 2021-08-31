cmake_minimum_required(VERSION 3.19)
project(vkquake)

set(CMAKE_CXX_STANDARD 98)

### Enable/Disable codecs for streaming music support
set(USE_CODEC_WAVE 1)
set(USE_CODEC_FLAC 0)
set(USE_CODEC_MP3 0)
set(USE_CODEC_VORBIS 1)
set(USE_CODEC_OPUS 0)
# either mikmod, or xmp
set(USE_CODEC_MIKMOD 0)
set(USE_CODEC_XMP 0)
set(USE_CODEC_UMX 0)

if(APPLE)
    set(CMAKE_APPLE_SILICON_PROCESSOR ON)
    set(CMAKE_OSX_ARCHITECTURES "arm64")
    message("CMAKE_APPLE_SILICON_PROCESSOR: ${CMAKE_APPLE_SILICON_PROCESSOR}")
    message("CMAKE_OSX_ARCHITECTURES: ${CMAKE_OSX_ARCHITECTURES}")
    message("CMAKE_HOST_SYSTEM_PROCESSOR: ${CMAKE_HOST_SYSTEM_PROCESSOR}")
    message("CMAKE_SYSTEM_PROCESSOR: ${CMAKE_SYSTEM_PROCESSOR}")
    message("CMAKE_CROSSCOMPILING: ${CMAKE_CROSSCOMPILING}")
endif(APPLE)

if(WIN32)
set(WINDOWS_RES_DIR ${PROJECT_SOURCE_DIR}/Windows/)
	if( CMAKE_SIZEOF_VOID_P EQUAL 8 )
		set( COMPILER_ARCH 64 )
	else( CMAKE_SIZEOF_VOID_P EQUAL 4 ) 
		set( COMPILER_ARCH 32 )
	endif()
endif(WIN32)

if(WIN32)
	set(COMPILE_DEF
		WIN32
		_DEBUG
		_WINDOWS
		_CRT_NONSTDC_NO_DEPRECATE
		_CRT_SECURE_NO_WARNINGS
		_WINSOCK_DEPRECATED_NO_WARNINGS
		)
endif(WIN32)

add_compile_definitions(${COMPILE_DEF})

set(INCLUDES "")

if(WIN32)
	set(INCLUDES ${INCLUDES}
	${WINDOWS_RES_DIR}/SDL2/include
	${WINDOWS_RES_DIR}/misc/Include
	${WINDOWS_RES_DIR}/codecs/Include
	)
endif(WIN32)

include_directories(
    /usr/local/include/
    /opt/homebrew/include/SDL2
	${INCLUDES}
    )

set(LIBRARY_PATH
	/usr/local/lib/
	/opt/homebrew/lib
)

if(WIN32)
	# Windows deps are bundled with the depot
	if(COMPILER_ARCH EQUAL 64)
		set(WINDOWS_LIBRARY_PATH
			${WINDOWS_RES_DIR}/SDL2/lib64
			${WINDOWS_RES_DIR}/misc/x64
			${WINDOWS_RES_DIR}/codecs/x64
		)
	else()
		set(WINDOWS_LIBRARY_PATH
			${WINDOWS_RES_DIR}/SDL2/lib
			${WINDOWS_RES_DIR}/misc/x86
			${WINDOWS_RES_DIR}/codecs/x86
		)
	endif()
	set(LIBRARY_PATH ${LIBRARY_PATH} ${WINDOWS_LIBRARY_PATH})
endif(WIN32)

# VULKAN
find_library(VULKAN_LIBRARY NAMES vulkan vulkan-1 PATHS ${LIBRARY_PATH} REQUIRED)	

# SDL
find_library(SDL2_LIBRARY NAMES SDL2 sdl2 PATHS ${LIBRARY_PATH} REQUIRED)
find_library(SDL2MAIN_LIBRARY NAMES SDL2main PATHS ${LIBRARY_PATH} REQUIRED)
set (SDL2_LIBRARY ${SDL2_LIBRARY} ${SDL2MAIN_LIBRARY})

# WAVE
if(USE_CODEC_WAVE EQUAL 1)
add_compile_definitions(USE_CODEC_WAVE)
endif()

# MP3
if(USE_CODEC_MP3 EQUAL 1)
add_compile_definitions(USE_CODEC_MP3)
endif()

# VORBIS
if(USE_CODEC_VORBIS EQUAL 1)
find_library(VORBIS_LIBRARY NAMES libvorbis PATHS ${LIBRARY_PATH} REQUIRED)
find_library(VORBISFILE_LIBRARY NAMES libvorbisfile PATHS ${LIBRARY_PATH} REQUIRED)
find_library(OGG_LIBRARY NAMES libogg PATHS ${LIBRARY_PATH} REQUIRED)
set (CODECS_LIBS ${CODECS_LIBS} ${VORBIS_LIBRARY} ${VORBISFILE_LIBRARY} ${OGG_LIBRARY})
add_compile_definitions(USE_CODEC_VORBIS)
endif()

# OPUS
if(USE_CODEC_OPUS EQUAL 1)
find_library(OPUS_LIBRARY NAMES libopus PATHS ${LIBRARY_PATH} REQUIRED)
find_library(OPUSFILE_LIBRARY NAMES libopusfile PATHS ${LIBRARY_PATH} REQUIRED)
set (CODECS_LIBS ${CODECS_LIBS} ${OPUS_LIBRARY} ${OPUSFILE_LIBRARY})
add_compile_definitions(USE_CODEC_OPUS)
endif()

# FLAC
if(USE_CODEC_FLAC EQUAL 1)
find_library(FLAC_LIBRARY NAMES libFLAC PATHS ${LIBRARY_PATH} REQUIRED)
set (CODECS_LIBS ${CODECS_LIBS} ${FLAC_LIBRARY})
add_compile_definitions(USE_CODEC_FLAC)
endif()

# MAD
if(USE_CODEC_MP3 EQUAL 1)
#TODO MAD or MPG123
find_library(MAD_LIBRARY NAMES libmad PATHS ${LIBRARY_PATH} REQUIRED)
set (CODECS_LIBS ${CODECS_LIBS} ${MAD_LIBRARY})
add_compile_definitions(USE_CODEC_MP3)
endif()

# MIKMOD
if(USE_CODEC_MIKMOD EQUAL 1)
find_library(MIKMOD_LIBRARY NAMES libmikmod PATHS ${LIBRARY_PATH} REQUIRED)
set (CODECS_LIBS ${CODECS_LIBS} ${MIKMOD_LIBRARY})
add_compile_definitions(USE_CODEC_MIKMOD)
endif()

if(USE_CODEC_XMP EQUAL 1)
add_compile_definitions(USE_CODEC_XMP)
endif()

if(USE_CODEC_UMX EQUAL 1)
add_compile_definitions(USE_CODEC_UMX)
endif()


if(WIN32)
	set(PLATFORM_LIB ws2_32.lib winmm.lib)
endif()

link_directories(${LIBRARY_PATH})


file(GLOB MUSIC_OBJS
    quake/bgmusic.c
    quake/snd_codec.c
    quake/snd_flac.c
    quake/snd_wave.c
    quake/snd_vorbis.c
    quake/snd_opus.c
    quake/snd_mp3.c
    quake/snd_mp3tag.c
    quake/snd_mikmod.c
    quake/snd_xmp.c
    quake/snd_umx.c
    )

file(GLOB SYSOBJ_INPUT quake/in_sdl.c)
file(GLOB COMOBJ_SND quake/snd_dma.c quake/snd_mix.c quake/snd_mem.c ${MUSIC_OBJS})
file(GLOB SYSOBJ_SND quake/snd_sdl.c)
file(GLOB SYSOBJ_CDA quake/cd_sdl.c)
file(GLOB SYSOBJ_GL_VID quake/gl_vidsdl.c)
file(GLOB SYSOBJ_NET quake/net_bsd.c quake/net_udp.c)
file(GLOB SYSOBJ_MAIN quake/main_sdl.c)

# SYS
if(APPLE OR LINUX)
file(GLOB SYSOBJ_SYS quake/pl_linux.c quake/sys_sdl_unix.c)
elseif(WIN32)
file(GLOB SYSOBJ_SYS quake/pl_win.c quake/sys_sdl_win.c)
endif()

# NET
if(APPLE OR LINUX)
file(GLOB SYSOBJ_NET quake/net_bsd.c quake/net_udp.c)
elseif(WIN32)
file(GLOB SYSOBJ_NET quake/net_win.c quake/net_wins.c quake/net_wipx.c)
endif()

file(GLOB SHADER_OBJS
	Shaders/Compiled/alias_frag.c
	Shaders/Compiled/alias_alphatest_frag.c
	Shaders/Compiled/alias_vert.c
	Shaders/Compiled/basic_alphatest_frag.c
	Shaders/Compiled/screen_effects_8bit_comp.c
	Shaders/Compiled/screen_effects_8bit_scale_comp.c
	Shaders/Compiled/screen_effects_8bit_scale_sops_comp.c
	Shaders/Compiled/screen_effects_10bit_comp.c
	Shaders/Compiled/screen_effects_10bit_scale_comp.c
	Shaders/Compiled/screen_effects_10bit_scale_sops_comp.c
	Shaders/Compiled/cs_tex_warp_comp.c
	Shaders/Compiled/basic_frag.c
	Shaders/Compiled/basic_notex_frag.c
	Shaders/Compiled/basic_vert.c
	Shaders/Compiled/sky_layer_frag.c
	Shaders/Compiled/sky_layer_vert.c
	Shaders/Compiled/sky_box_frag.c
	Shaders/Compiled/postprocess_frag.c
	Shaders/Compiled/postprocess_vert.c
	Shaders/Compiled/world_frag.c
	Shaders/Compiled/world_vert.c
	Shaders/Compiled/showtris_frag.c
	Shaders/Compiled/showtris_vert.c
    )

file(GLOB GLOBJS
	${SHADER_OBJS}
	quake/gl_refrag.c
	quake/gl_rlight.c
	quake/gl_rmain.c
	quake/gl_fog.c
	quake/gl_rmisc.c
	quake/r_part.c
	quake/r_world.c
	quake/gl_screen.c
	quake/gl_sky.c
	quake/gl_warp.c
	${SYSOBJ_GL_VID}
	quake/gl_draw.c
	quake/image.c
	quake/gl_texmgr.c
	quake/gl_mesh.c
	quake/gl_heap.c
	quake/r_sprite.c
	quake/r_alias.c
	quake/r_brush.c
	quake/gl_model.c
    )

if(MSVC)
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /SUBSYSTEM:WINDOWS")
endif(MSVC)

add_executable(${PROJECT_NAME}
    # Source
    quake/strlcat.c
	quake/strlcpy.c
	${GLOBJS}
	${SYSOBJ_INPUT}
	${COMOBJ_SND}
	${SYSOBJ_SND}
	${SYSOBJ_CDA}
	${SYSOBJ_NET}
	quake/net_dgrm.c
	quake/net_loop.c
	quake/net_main.c
	quake/chase.c
	quake/cl_demo.c
	quake/cl_input.c
	quake/cl_main.c
	quake/cl_parse.c
	quake/cl_tent.c
	quake/console.c
	quake/keys.c
	quake/menu.c
	quake/sbar.c
	quake/view.c
	quake/wad.c
	quake/cmd.c
	quake/common.c
	quake/crc.c
	quake/cvar.c
	quake/cfgfile.c
	quake/host.c
	quake/host_cmd.c
	quake/mathlib.c
	quake/mdfour.c
	quake/pr_cmds.c
	quake/pr_ext.c
	quake/pr_edict.c
	quake/pr_exec.c
	quake/sv_main.c
	quake/sv_move.c
	quake/sv_phys.c
	quake/sv_user.c
	quake/world.c
	quake/zone.c
	${SYSOBJ_SYS} ${SYSOBJ_MAIN}
    )

target_link_libraries(${PROJECT_NAME}
	${PLATFORM_LIB}
    ${VULKAN_LIBRARY}
	${CODECS_LIBS}
	${SDL2_LIBRARY}
    )