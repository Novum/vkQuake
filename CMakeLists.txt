cmake_minimum_required(VERSION 3.19)
project(vkquake)

set(CMAKE_CXX_STANDARD 98)

if(APPLE)
    set(CMAKE_APPLE_SILICON_PROCESSOR ON)
    set(CMAKE_OSX_ARCHITECTURES "arm64")
    message("CMAKE_APPLE_SILICON_PROCESSOR: ${CMAKE_APPLE_SILICON_PROCESSOR}")
    message("CMAKE_OSX_ARCHITECTURES: ${CMAKE_OSX_ARCHITECTURES}")
    message("CMAKE_HOST_SYSTEM_PROCESSOR: ${CMAKE_HOST_SYSTEM_PROCESSOR}")
    message("CMAKE_SYSTEM_PROCESSOR: ${CMAKE_SYSTEM_PROCESSOR}")
    message("CMAKE_CROSSCOMPILING: ${CMAKE_CROSSCOMPILING}")
endif(APPLE)

if(MSVC)
set(WINDOWS_RES_DIR ${PROJECT_SOURCE_DIR}/Windows/)
endif(MSVC)

if(MSVC)
	set(COMPILE_DEF
		WIN32
		_DEBUG
		_WINDOWS
		_CRT_NONSTDC_NO_DEPRECATE
		_CRT_SECURE_NO_WARNINGS
		_WINSOCK_DEPRECATED_NO_WARNINGS
		USE_CODEC_WAVE
		#USE_CODEC_VORBIS
		#sUSE_CODEC_MP3
		#USE_CODEC_FLAC
		#USE_CODEC_OPUS
		#USE_CODEC_MIKMOD
		#USE_CODEC_UMX
		)
endif(MSVC)

add_compile_definitions(
	${COMPILE_DEF}
    )

set(INCLUDES "")

if(MSVC)
	set(INCLUDES ${INCLUDES}
	${WINDOWS_RES_DIR}/SDL2/include
	C:/VulkanSDK/1.2.182.0/Include
	)
endif(MSVC)

include_directories(
    /usr/local/include/
    /opt/homebrew/include/SDL2
	${INCLUDES}
    )

link_directories(
    /usr/local/lib/
    /opt/homebrew/lib
    )

# Libraries
#find_library(VULKAN_LIBRARY NAMES vulkan vulkan-1)
#set(LIB_VULKAN ${VULKAN_LIBRARY})

#find_library(SDL_LIBRARY NAMES SDL2)
#set(LIB_SDL ${SDL_LIBRARY})
#find_package(SDL2 REQUIRED)

if (MSVC)

	find_library(VULKAN_LIBRARY_64 NAMES vulkan vulkan-1
		PATHS C:/VulkanSDK/1.2.182.0/lib
		REQUIRED
	)
	
	find_library(VULKAN_LIBRARY_32 NAMES vulkan vulkan-1
		PATHS C:/VulkanSDK/1.2.182.0/lib32
		REQUIRED
	)

	find_library(SDL2_LIBRARY_RELEASE_64 NAMES SDL2 sdl2 
		PATHS ${WINDOWS_RES_DIR}/SDL2/lib64
		NO_DEFAULT_PATH
		REQUIRED
	)

	find_library(SDL2_LIBRARY_RELEASE_32 NAMES SDL2 sdl2 
	PATHS ${WINDOWS_RES_DIR}/SDL2/lib
	NO_DEFAULT_PATH
	REQUIRED
)
endif(MSVC)

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
if(APPLE)
file(GLOB SYSOBJ_SYS quake/pl_linux.c quake/sys_sdl_unix.c)
elseif(MSVC)
file(GLOB SYSOBJ_SYS quake/pl_win.c quake/sys_sdl_win.c)
endif()

# NET
if(APPLE)
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

IF(MSVC)
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /NODEFAULTLIB:MSVCRT.lib /SUBSYSTEM:WINDOWS")
set (MSVC_RUNTIME_LIBRARY MultiThreadedDebugDLL)
ENDIF(MSVC)

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

set(PLATFORM_LIB
	#libvorbisfile.lib
	#libvorbis.lib
	#libopusfile.lib
	#libopus.lib
	#libFLAC.lib
	#libogg.lib
	#libmad.lib
	#libmikmod.lib
	ws2_32.lib
	winmm.lib
	)

target_link_libraries(${PROJECT_NAME}
	${PLATFORM_LIB}
    ${VULKAN_LIBRARY_64}
	${SDL2_LIBRARY_RELEASE_64}
    )