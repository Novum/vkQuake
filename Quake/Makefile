# GNU Makefile for vkQuake unix targets
# You need the SDL2 library fully installed.
# "make DEBUG=1" to build a debug client.
# "make SDL_CONFIG=/path/to/sdl2-config" for unusual SDL2 installations.
# "make DO_USERDIRS=1" to enable user directories support
# "make VULKAN_SDK=/path/to/sdk" if it is not already in path

# Enable/Disable user directories support
DO_USERDIRS=0

### Enable/Disable codecs for streaming music support
USE_CODEC_WAVE=1
USE_CODEC_FLAC=0
USE_CODEC_MP3=1
USE_CODEC_VORBIS=1
USE_CODEC_OPUS=0
# either xmp or mikmod (or modplug)
USE_CODEC_MIKMOD=0
USE_CODEC_XMP=0
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

CPUFLAGS=
LDFLAGS ?=
DFLAGS ?=
CFLAGS ?=

CFLAGS += -Wall -Wno-trigraphs -Wno-unused-function -Werror -std=gnu11 -MMD
CFLAGS += $(CPUFLAGS)
ifneq ($(DEBUG),0)
DFLAGS += -D_DEBUG
CFLAGS += -g
do_strip=
else
DFLAGS += -DNDEBUG
CFLAGS += -O3
CFLAGS += $(call check_gcc,-fweb,)
CFLAGS += $(call check_gcc,-frename-registers,)
CFLAGS += $(call check_gcc,-fno-asynchronous-unwind-tables,)
CFLAGS += $(call check_gcc,-fno-ident,)
cmd_strip=$(STRIP) $(1)
define do_strip
	$(call cmd_strip,$(1));
endef
endif

ifneq ($(VULKAN_SDK),)
LDFLAGS += -L$(VULKAN_SDK)/lib
CFLAGS += -I$(VULKAN_SDK)/include
endif

ifeq ($(DO_USERDIRS),1)
CFLAGS += -DDO_USERDIRS=1
endif

SDL_CONFIG ?= sdl2-config
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
mp3_obj=snd_mp3
lib_mp3dec=-lmad
endif
ifeq ($(MP3LIB),mpg123)
mp3_obj=snd_mpg123
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
ifeq ($(USE_CODEC_XMP),1)
CFLAGS+= -DUSE_CODEC_XMP
CODECLIBS+= -lxmp
endif
ifeq ($(USE_CODEC_MODPLUG),1)
CFLAGS+= -DUSE_CODEC_MODPLUG
CODECLIBS+= -lmodplug
endif
ifeq ($(USE_CODEC_UMX),1)
CFLAGS+= -DUSE_CODEC_UMX
endif

COMMON_LIBS:= -lm -lpthread

ifneq ($(HOST_OS),darwin)
COMMON_LIBS+= -lvulkan
CFLAGS+= -DLINUX
else
ifeq ($(shell test -d /opt/homebrew && echo true),true)
COMMON_LIBS+= -L/opt/homebrew/lib
CFLAGS+= -I/opt/homebrew/include
endif
COMMON_LIBS+= -lMoltenVK
endif

LIBS := $(COMMON_LIBS) $(NET_LIBS) $(CODECLIBS)

# ---------------------------
# objects
# ---------------------------

MUSIC_OBJS:= bgmusic.o \
	snd_codec.o \
	snd_flac.o \
	snd_wave.o \
	snd_vorbis.o \
	snd_opus.o \
	$(mp3_obj).o \
	snd_mp3tag.o \
	snd_mikmod.o \
	snd_modplug.o \
	snd_xmp.o \
	snd_umx.o
COMOBJ_SND := snd_dma.o snd_mix.o snd_mem.o $(MUSIC_OBJS)
SYSOBJ_SND := snd_sdl.o
SYSOBJ_CDA := cd_sdl.o
SYSOBJ_INPUT := in_sdl.o
SYSOBJ_GL_VID:= gl_vidsdl.o
SYSOBJ_NET := net_bsd.o net_udp.o
SYSOBJ_SYS := pl_linux.o sys_sdl.o sys_sdl_unix.o
SYSOBJ_MAIN:= main_sdl.o

SHADER_OBJS = \
	alias.frag.o \
	alias_alphatest.frag.o \
	alias.vert.o \
	basic_alphatest.frag.o \
	screen_effects_8bit.comp.o \
	screen_effects_8bit_scale.comp.o \
	screen_effects_8bit_scale_sops.comp.o \
	screen_effects_10bit.comp.o \
	screen_effects_10bit_scale.comp.o \
	screen_effects_10bit_scale_sops.comp.o \
	cs_tex_warp.comp.o \
	basic.frag.o \
	basic_notex.frag.o \
	basic.vert.o \
	sky_layer.frag.o \
	sky_layer.vert.o \
	sky_box.frag.o \
	postprocess.frag.o \
	postprocess.vert.o \
	world.frag.o \
	world.vert.o \
	showtris.frag.o \
	showtris.vert.o \
	update_lightmap.comp.o

GLOBJS = \
	$(SHADER_OBJS) \
	palette.o \
	gl_refrag.o \
	gl_rlight.o \
	gl_rmain.o \
	gl_fog.o \
	gl_rmisc.o \
	r_part.o \
	r_part_fte.o \
	r_world.o \
	gl_screen.o \
	gl_sky.o \
	gl_warp.o \
	$(SYSOBJ_GL_VID) \
	gl_draw.o \
	image.o \
	gl_texmgr.o \
	gl_mesh.o \
	gl_heap.o \
	r_sprite.o \
	r_alias.o \
	r_brush.o \
	gl_model.o

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
	miniz.o \
	crc.o \
	cvar.o \
	cfgfile.o \
	host.o \
	host_cmd.o \
	mathlib.o \
	mdfour.o \
	pr_cmds.o \
	pr_ext.o \
	pr_edict.o \
	pr_exec.o \
	sv_main.o \
	sv_move.o \
	sv_phys.o \
	sv_user.o \
	world.o \
	mem.o \
	tasks.o \
	embedded_pak.o \
	$(SYSOBJ_SYS) $(SYSOBJ_MAIN)

# ---------------------------
# targets / rules
# ---------------------------

.PHONY:	clean debug release

DEFAULT_TARGET := vkquake
all: $(DEFAULT_TARGET)

%.o:	%.c
	$(CC) $(DFLAGS) -c $(CFLAGS) $(SDL_CFLAGS) -o $@ $<
ifeq ($(DEBUG),0)
%.o:	../Shaders/Compiled/Release/%.c
	$(CC) $(DFLAGS) -c $(CFLAGS) $(SDL_CFLAGS) -o $@ $^
else
%.o:	../Shaders/Compiled/Debug/%.c
	$(CC) $(DFLAGS) -c $(CFLAGS) $(SDL_CFLAGS) -o $@ $^
endif

vkquake:	$(OBJS)
	$(LINKER) $(OBJS) $(LDFLAGS) $(LIBS) $(SDL_LIBS) -o $@
	$(call do_strip,$@)

release:	vkquake
debug:
	$(error Use "make DEBUG=1")

clean:
	$(RM) *.o *.d $(DEFAULT_TARGET)

prefix ?= /usr
exec_prefix ?= $(prefix)
bindir ?= $(exec_prefix)/games
sbindir ?= $(exec_prefix)/sbin
INSTALL ?= install
INSTALL_PROGRAM ?= $(INSTALL)
INSTALL_DATA ?= ${INSTALL} -m 644
install: vkquake
	$(INSTALL_PROGRAM) $(CURDIR)/vkquake $(DESTDIR)$(bindir)/vkquake

sinclude $(OBJS:.o=.d)
