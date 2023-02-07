CC = gcc
# HOST_CC is for bintoc which is run on the host OS, not
#         the target OS: cross-compile friendliness.
HOST_CC = gcc
LINKER = $(CC)
STRIP ?= strip
GLSLANG = glslangValidator
DEBUG ?= 0

CHECK_GCC = $(shell if echo | $(CC) $(1) -Werror -S -o /dev/null -xc - > /dev/null 2>&1; then echo "$(1)"; else echo "$(2)"; fi;)

CFLAGS += -MMD -Wall -Wno-trigraphs -Werror -std=gnu11
CFLAGS += $(CPUFLAGS)
ifneq ($(DEBUG),0)
DFLAGS += -D_DEBUG
CFLAGS += -g
DO_STRIP=
else
DFLAGS += -DNDEBUG
CFLAGS += -O3
CFLAGS += $(call CHECK_GCC,-fweb,)
CFLAGS += $(call CHECK_GCC,-frename-registers,)
CFLAGS += $(call CHECK_GCC,-fno-asynchronous-unwind-tables,)
CFLAGS += $(call CHECK_GCC,-fno-ident,)
CMD_STRIP=$(STRIP) $(1)
define DO_STRIP
	$(call CMD_STRIP,$(1));
endef
endif

ifeq ($(DEBUG),0)
GLSLANG_FLAGS = -V
GLSLANG_OUT_FOLDER = Release
else
GLSLANG_FLAGS = -g -V
GLSLANG_OUT_FOLDER = Debug
endif

# ---------------------------
# libraries
# ---------------------------
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
mp3_obj = snd_mp3
lib_mp3dec = -lmad
endif
ifeq ($(MP3LIB),mpg123)
mp3_obj = snd_mpg123
lib_mp3dec = -lmpg123
endif
ifeq ($(VORBISLIB),vorbis)
CPP_VORBISDEC =
LIB_VORBISDEC = -lvorbisfile -lvorbis -logg
endif
ifeq ($(VORBISLIB),tremor)
CPP_VORBISDEC = -DVORBIS_USE_TREMOR
LIB_VORBISDEC = -lvorbisidec -logg
endif

CODECLIBS  :=
ifeq ($(USE_CODEC_WAVE),1)
CFLAGS += -DUSE_CODEC_WAVE
endif
ifeq ($(USE_CODEC_FLAC),1)
CFLAGS += -DUSE_CODEC_FLAC
CODECLIBS += -lFLAC
endif
ifeq ($(USE_CODEC_OPUS),1)
CFLAGS += -DUSE_CODEC_OPUS
endif
ifeq ($(USE_CODEC_VORBIS),1)
CFLAGS += -DUSE_CODEC_VORBIS $(CPP_VORBISDEC)
CODECLIBS += $(LIB_VORBISDEC)
endif
ifeq ($(USE_CODEC_MP3),1)
CFLAGS += -DUSE_CODEC_MP3
CODECLIBS += $(lib_mp3dec)
endif
ifeq ($(USE_CODEC_MIKMOD),1)
CFLAGS += -DUSE_CODEC_MIKMOD
CODECLIBS += -lmikmod
endif
ifeq ($(USE_CODEC_XMP),1)
CFLAGS += -DUSE_CODEC_XMP
CODECLIBS += -lxmp
endif
ifeq ($(USE_CODEC_MODPLUG),1)
CFLAGS += -DUSE_CODEC_MODPLUG
CODECLIBS += -lmodplug
endif
ifeq ($(USE_CODEC_UMX),1)
CFLAGS += -DUSE_CODEC_UMX
endif

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
SYSOBJ_MAIN:= main_sdl.o

SHADER_OBJS = \
	alias_frag.o \
	alias_alphatest_frag.o \
	alias_vert.o \
	md5_vert.o \
	basic_alphatest_frag.o \
	screen_effects_8bit_comp.o \
	screen_effects_8bit_scale_comp.o \
	screen_effects_8bit_scale_sops_comp.o \
	screen_effects_10bit_comp.o \
	screen_effects_10bit_scale_comp.o \
	screen_effects_10bit_scale_sops_comp.o \
	cs_tex_warp_comp.o \
	indirect_comp.o \
	indirect_clear_comp.o \
	basic_frag.o \
	basic_notex_frag.o \
	basic_vert.o \
	sky_layer_frag.o \
	sky_layer_vert.o \
	sky_box_frag.o \
	sky_cube_frag.o \
	sky_cube_vert.o \
	postprocess_frag.o \
	postprocess_vert.o \
	world_frag.o \
	world_vert.o \
	showtris_frag.o \
	showtris_vert.o \
	update_lightmap_comp.o \
	update_lightmap_rt_comp.o \
	ray_debug_comp.o

GLOBJS = \
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
	hash_map.o \
	embedded_pak.o \
	$(SYSOBJ_SYS) $(SYSOBJ_MAIN)

$(BINTOC_EXE): ../Shaders/bintoc.c
	$(HOST_CC) -o $@ $<

.SECONDARY:
../Shaders/Compiled/$(GLSLANG_OUT_FOLDER)/%_frag.spv: ../Shaders/%.frag
	$(GLSLANG) $(GLSLANG_FLAGS) $< -o $@ --depfile ../Shaders/Compiled/$(GLSLANG_OUT_FOLDER)/$*_frag.d
%_frag.o: ../Shaders/Compiled/$(GLSLANG_OUT_FOLDER)/%_frag.spv $(BINTOC_EXE)
	$(BINTOC_EXE) $< $*_frag_spv ../Shaders/Compiled/$(GLSLANG_OUT_FOLDER)/$*_frag.c
	$(CC) $(DFLAGS) -c $(CFLAGS) $(SDL_CFLAGS) -o $@ ../Shaders/Compiled/$(GLSLANG_OUT_FOLDER)/$*_frag.c

.SECONDARY:
../Shaders/Compiled/$(GLSLANG_OUT_FOLDER)/%_vert.spv: ../Shaders/%.vert
	$(GLSLANG) $(GLSLANG_FLAGS) $< -o $@ --depfile ../Shaders/Compiled/$(GLSLANG_OUT_FOLDER)/$*_vert.d
%_vert.o: ../Shaders/Compiled/$(GLSLANG_OUT_FOLDER)/%_vert.spv $(BINTOC_EXE)
	$(BINTOC_EXE) $< $*_vert_spv ../Shaders/Compiled/$(GLSLANG_OUT_FOLDER)/$*_vert.c
	$(CC) $(DFLAGS) -c $(CFLAGS) $(SDL_CFLAGS) -o $@ ../Shaders/Compiled/$(GLSLANG_OUT_FOLDER)/$*_vert.c

.SECONDARY:
../Shaders/Compiled/$(GLSLANG_OUT_FOLDER)/%_sops_comp.spv: ../Shaders/%_sops.comp
	$(GLSLANG) $(GLSLANG_FLAGS) --target-env vulkan1.1 $< -o $@ --depfile ../Shaders/Compiled/$(GLSLANG_OUT_FOLDER)/$*_sops_comp.d
%_sops_comp.o: ../Shaders/Compiled/$(GLSLANG_OUT_FOLDER)/%_sops_comp.spv $(BINTOC_EXE)
	$(BINTOC_EXE) $< $*_sops_comp_spv ../Shaders/Compiled/$(GLSLANG_OUT_FOLDER)/$*_sops_comp.c
	$(CC) $(DFLAGS) -c $(CFLAGS) $(SDL_CFLAGS) -o $@ ../Shaders/Compiled/$(GLSLANG_OUT_FOLDER)/$*_sops_comp.c

.SECONDARY:
../Shaders/Compiled/$(GLSLANG_OUT_FOLDER)/%_comp.spv: ../Shaders/%.comp
	$(GLSLANG) $(GLSLANG_FLAGS) $< -o $@ --depfile ../Shaders/Compiled/$(GLSLANG_OUT_FOLDER)/$*_comp.d
%_comp.o: ../Shaders/Compiled/$(GLSLANG_OUT_FOLDER)/%_comp.spv $(BINTOC_EXE)
	$(BINTOC_EXE) $< $*_comp_spv ../Shaders/Compiled/$(GLSLANG_OUT_FOLDER)/$*_comp.c
	$(CC) $(DFLAGS) -c $(CFLAGS) $(SDL_CFLAGS) -o $@ ../Shaders/Compiled/$(GLSLANG_OUT_FOLDER)/$*_comp.c

%.o:	%.c
	$(CC) $(DFLAGS) -c $(CFLAGS) $(SDL_CFLAGS) -o $@ $<

sinclude $(OBJS:.o=.d)
sinclude $(SHADER_OBJS:%.o=../Shaders/Compiled/$(GLSLANG_OUT_FOLDER)/%.d)

.PHONY:	clean debug release
.DEFAULT_GOAL := $(DEFAULT_TARGET)
