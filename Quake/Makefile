# GNU Makefile for vkQuake unix targets, Sep. 10, 2014
# You need the SDL library fully installed.
# "make DEBUG=1" to build a debug client.
# "make SDL_CONFIG=/path/to/sdl-config" for unusual SDL installations.
# "make DO_USERDIRS=1" to enable user directories support

# Enable/Disable user directories support
DO_USERDIRS=0

### Vulkan SDK directory
VULKAN_INCLUDE_DIR=$(VULKAN_SDK)/include

### Enable/Disable SDL2
USE_SDL2=1

### Enable/Disable codecs for streaming music support
USE_CODEC_WAVE=1
USE_CODEC_FLAC=0
USE_CODEC_MP3=1
USE_CODEC_VORBIS=1
USE_CODEC_OPUS=0
# either mikmod (preferred) or modplug, not both
USE_CODEC_MIKMOD=0
USE_CODEC_MODPLUG=0
USE_CODEC_UMX=0

# which library to use for mp3 decoding: mad or mpg123
MP3LIB=mad
# which library to use for ogg decoding: vorbis or tremor
VORBISLIB=vorbis

# ---------------------------
# Helper functions
# ---------------------------

check_gcc = $(shell if echo | $(CC) $(1) -Werror -S -o /dev/null -xc - > /dev/null 2>&1; then echo "$(1)"; else echo "$(2)"; fi;)

# ---------------------------

HOST_OS := $(shell uname|sed -e s/_.*//|tr '[:upper:]' '[:lower:]')

DEBUG   ?= 0

# ---------------------------
# build variables
# ---------------------------

CC ?= gcc
LINKER = $(CC)

STRIP ?= strip

#CPUFLAGS= -mtune=i686
#CPUFLAGS= -march=pentium4
#CPUFLAGS= -mtune=k8
#CPUFLAGS= -march=atom
CPUFLAGS=
LDFLAGS = -L$(VULKAN_SDK)/lib
DFLAGS ?=
CFLAGS ?= -Wall -Wno-trigraphs
CFLAGS += $(CPUFLAGS)
ifneq ($(DEBUG),0)
DFLAGS += -D_DEBUG
CFLAGS += -g
do_strip=
else
DFLAGS += -DNDEBUG
CFLAGS += -O2
CFLAGS += $(call check_gcc,-fweb,)
CFLAGS += $(call check_gcc,-frename-registers,)
cmd_strip=$(STRIP) $(1)
define do_strip
	$(call cmd_strip,$(1));
endef
endif
CFLAGS += -std=gnu99
CFLAGS += -I$(VULKAN_INCLUDE_DIR) -DLINUX -DVK_USE_PLATFORM_XCB_KHR

ifeq ($(DO_USERDIRS),1)
CFLAGS += -DDO_USERDIRS=1
endif

### X11BASE only gets used if its in an unusual place

X11DIRS := /usr/X11R7 /usr/local/X11R7 /usr/X11R6 /usr/local/X11R6
X11BASE_GUESS := $(shell \
	if [ -e /usr/include/X11/Xlib.h ] && \
		[ -e /usr/lib/libX11.a ]; then exit 0; fi; \
	if [ -e /usr/local/include/X11/Xlib.h ] && \
		[ -e /usr/local/lib/libX11.a ]; then exit 0; fi; \
	for DIR in $(X11DIRS); do \
            if [ -e $$DIR/include/X11/Xlib.h ] && \
		[ -e $$DIR/lib/libX11.a ]; then echo $$DIR; break; fi; \
	done )

X11BASE	?= $(X11BASE_GUESS)

ifneq ($(X11BASE),)
LDFLAGS+= -L$(X11BASE)/lib
CFLAGS += -I$(X11BASE)/include
endif

ifeq ($(USE_SDL2),1)
CFLAGS += -DUSE_SDL2
endif

ifeq ($(USE_SDL2),1)
SDL_CONFIG ?= sdl2-config
else
SDL_CONFIG ?= sdl-config
endif
SDL_CFLAGS := $(shell $(SDL_CONFIG) --cflags)
SDL_LIBS   := $(shell $(SDL_CONFIG) --libs)

ifeq ($(HOST_OS),sunos)
NET_LIBS   :=-lsocket -lnsl -lresolv
else
NET_LIBS   :=
endif

ifneq ($(VORBISLIB),vorbis)
ifneq ($(VORBISLIB),tremor)
$(error Invalid VORBISLIB setting)
endif
endif
ifneq ($(MP3LIB),mpg123)
ifneq ($(MP3LIB),mad)
$(error Invalid MP3LIB setting)
endif
endif
ifeq ($(MP3LIB),mad)
mp3_obj=snd_mp3.o
lib_mp3dec=-lmad
endif
ifeq ($(MP3LIB),mpg123)
mp3_obj=snd_mpg123.o
lib_mp3dec=-lmpg123
endif
ifeq ($(VORBISLIB),vorbis)
cpp_vorbisdec=
lib_vorbisdec=-lvorbisfile -lvorbis -logg
endif
ifeq ($(VORBISLIB),tremor)
cpp_vorbisdec=-DVORBIS_USE_TREMOR
lib_vorbisdec=-lvorbisidec -logg
endif

CODECLIBS  :=
ifeq ($(USE_CODEC_WAVE),1)
CFLAGS+= -DUSE_CODEC_WAVE
endif
ifeq ($(USE_CODEC_FLAC),1)
CFLAGS+= -DUSE_CODEC_FLAC
CODECLIBS+= -lFLAC
endif
ifeq ($(USE_CODEC_OPUS),1)
# opus and opusfile put their *.h under <includedir>/opus,
# but they include the headers without the opus directory
# prefix and rely on pkg-config. ewww...
CFLAGS+= -DUSE_CODEC_OPUS
CFLAGS+= $(shell pkg-config --cflags opusfile)
CODECLIBS+= $(shell pkg-config --libs   opusfile)
endif
ifeq ($(USE_CODEC_VORBIS),1)
CFLAGS+= -DUSE_CODEC_VORBIS $(cpp_vorbisdec)
CODECLIBS+= $(lib_vorbisdec)
endif
ifeq ($(USE_CODEC_MP3),1)
CFLAGS+= -DUSE_CODEC_MP3
CODECLIBS+= $(lib_mp3dec)
endif
ifeq ($(USE_CODEC_MIKMOD),1)
CFLAGS+= -DUSE_CODEC_MIKMOD
CODECLIBS+= -lmikmod
endif
ifeq ($(USE_CODEC_MODPLUG),1)
CFLAGS+= -DUSE_CODEC_MODPLUG
CODECLIBS+= -lmodplug
endif
ifeq ($(USE_CODEC_UMX),1)
CFLAGS+= -DUSE_CODEC_UMX
endif

COMMON_LIBS:= -lm -l:libvulkan.so.1 -lX11-xcb

LIBS := $(COMMON_LIBS) $(NET_LIBS) $(CODECLIBS)

# ---------------------------
# targets
# ---------------------------

.PHONY:	clean debug release

DEFAULT_TARGET := vkquake

# ---------------------------
# rules
# ---------------------------

%.o:	%.c
	$(CC) $(DFLAGS) -c $(CFLAGS) $(SDL_CFLAGS) -o $@ $^

# ----------------------------------------------------------------------------
# objects
# ----------------------------------------------------------------------------

MUSIC_OBJS:= bgmusic.o \
	snd_codec.o \
	snd_flac.o \
	snd_wave.o \
	snd_vorbis.o \
	snd_opus.o \
	$(mp3_obj) \
	snd_mikmod.o \
	snd_modplug.o \
	snd_umx.o
COMOBJ_SND := snd_dma.o snd_mix.o snd_mem.o $(MUSIC_OBJS)
SYSOBJ_SND := snd_sdl.o
SYSOBJ_CDA := cd_sdl.o
SYSOBJ_INPUT := in_sdl.o
SYSOBJ_GL_VID:= gl_vidsdl.o
SYSOBJ_NET := net_bsd.o net_udp.o
SYSOBJ_SYS := pl_linux.o sys_sdl_unix.o
SYSOBJ_MAIN:= main_sdl.o
SYSOBJ_RES :=

SHADER_OBJS = \
	../Shaders/Compiled/alias_frag.c \
	../Shaders/Compiled/alias_vert.c \
	../Shaders/Compiled/basic_alphatest_frag.c \
	../Shaders/Compiled/basic_char_frag.c \
	../Shaders/Compiled/basic_frag.c \
	../Shaders/Compiled/basic_notex_frag.c \
	../Shaders/Compiled/basic_vert.c \
	../Shaders/Compiled/sky_layer_frag.c \
	../Shaders/Compiled/sky_layer_vert.c \
	../Shaders/Compiled/postprocess_frag.c \
	../Shaders/Compiled/postprocess_vert.c \
	../Shaders/Compiled/world_frag.c \
	../Shaders/Compiled/world_vert.c

GLOBJS = \
	gl_refrag.o \
	gl_rlight.o \
	gl_rmain.o \
	gl_fog.o \
	gl_rmisc.o \
	r_part.o \
	r_world.o \
	gl_screen.o \
	gl_sky.o \
	gl_warp.o \
	$(SYSOBJ_GL_VID) \
	gl_draw.o \
	image.o \
	gl_texmgr.o \
	gl_mesh.o \
	r_sprite.o \
	r_alias.o \
	r_brush.o \
	gl_model.o \
	gl_heap.o

OBJS := strlcat.o \
	strlcpy.o \
	$(GLOBJS) \
	$(SYSOBJ_INPUT) \
	$(COMOBJ_SND) \
	$(SYSOBJ_SND) \
	$(SYSOBJ_CDA) \
	$(SYSOBJ_NET) \
	net_dgrm.o \
	net_loop.o \
	net_main.o \
	chase.o \
	cl_demo.o \
	cl_input.o \
	cl_main.o \
	cl_parse.o \
	cl_tent.o \
	console.o \
	keys.o \
	menu.o \
	sbar.o \
	view.o \
	wad.o \
	cmd.o \
	common.o \
	crc.o \
	cvar.o \
	cfgfile.o \
	host.o \
	host_cmd.o \
	mathlib.o \
	pr_cmds.o \
	pr_edict.o \
	pr_exec.o \
	sv_main.o \
	sv_move.o \
	sv_phys.o \
	sv_user.o \
	world.o \
	zone.o \
	$(SYSOBJ_SYS) $(SYSOBJ_MAIN) $(SYSOBJ_RES) $(SHADER_OBJS)

# ------------------------
# Linux build rules
# ------------------------

vkquake:	$(OBJS)
	$(LINKER) $(OBJS) $(LDFLAGS) $(LIBS) $(SDL_LIBS) -o $@
	$(call do_strip,$@)

release:	vkquake
debug:
	$(error Use "make DEBUG=1")

clean:
	rm -f $(shell find . \( -name '*~' -o -name '#*#' -o -name '*.o' -o -name '*.res' -o -name $(DEFAULT_TARGET) \) -print)

prefix ?= /usr
exec_prefix ?= $(prefix)
bindir ?= $(exec_prefix)/games
sbindir ?= $(exec_prefix)/sbin
INSTALL ?= install
INSTALL_PROGRAM ?= $(INSTALL)
INSTALL_DATA ?= ${INSTALL} -m 644
install: vkquake
	$(INSTALL_PROGRAM) $(CURDIR)/vkquake $(DESTDIR)$(bindir)/vkquake
