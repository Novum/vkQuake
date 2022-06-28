project('vkquake', 'c', default_options : ['c_std=gnu11'])

cflags = ['-Wall', '-Wno-trigraphs', '-Wno-unused-function', '-Werror']

srcs = [
    'Quake/bgmusic.c',
    'Quake/cd_null.c',
    'Quake/cfgfile.c',
    'Quake/chase.c',
    'Quake/cl_demo.c',
    'Quake/cl_input.c',
    'Quake/cl_main.c',
    'Quake/cl_parse.c',
    'Quake/cl_tent.c',
    'Quake/cmd.c',
    'Quake/common.c',
    'Quake/console.c',
    'Quake/crc.c',
    'Quake/cvar.c',
    'Quake/gl_draw.c',
    'Quake/gl_fog.c',
    'Quake/gl_heap.c',
    'Quake/gl_mesh.c',
    'Quake/gl_model.c',
    'Quake/gl_refrag.c',
    'Quake/gl_rlight.c',
    'Quake/gl_rmain.c',
    'Quake/gl_rmisc.c',
    'Quake/gl_screen.c',
    'Quake/gl_sky.c',
    'Quake/gl_texmgr.c',
    'Quake/gl_vidsdl.c',
    'Quake/gl_warp.c',
    'Quake/host.c',
    'Quake/host_cmd.c',
    'Quake/image.c',
    'Quake/in_sdl.c',
    'Quake/keys.c',
    'Quake/main_sdl.c',
    'Quake/mathlib.c',
    'Quake/mdfour.c',
    'Quake/mem.c',
    'Quake/menu.c',
    'Quake/miniz.c',
    'Quake/net_bsd.c',
    'Quake/net_dgrm.c',
    'Quake/net_loop.c',
    'Quake/net_main.c',
    'Quake/net_udp.c',
    'Quake/palette.c',
    'Quake/pl_linux.c',
    'Quake/pr_cmds.c',
    'Quake/pr_edict.c',
    'Quake/pr_exec.c',
    'Quake/pr_ext.c',
    'Quake/r_alias.c',
    'Quake/r_brush.c',
    'Quake/r_part.c',
    'Quake/r_part_fte.c',
    'Quake/r_sprite.c',
    'Quake/r_world.c',
    'Quake/sbar.c',
    'Quake/snd_codec.c',
    'Quake/snd_dma.c',
    'Quake/snd_mem.c',
    'Quake/snd_mix.c',
    'Quake/snd_sdl.c',
    'Quake/snd_umx.c',
    'Quake/snd_wave.c',
    'Quake/strlcat.c',
    'Quake/strlcpy.c',
    'Quake/sv_main.c',
    'Quake/sv_move.c',
    'Quake/sv_phys.c',
    'Quake/sv_user.c',
    'Quake/sys_sdl_unix.c',
    'Quake/tasks.c',
    'Quake/view.c',
    'Quake/wad.c',
    'Quake/world.c',
    'Shaders/Compiled/Release/alias_alphatest.frag.c',
    'Shaders/Compiled/Release/alias.frag.c',
    'Shaders/Compiled/Release/alias.vert.c',
    'Shaders/Compiled/Release/basic_alphatest.frag.c',
    'Shaders/Compiled/Release/basic.frag.c',
    'Shaders/Compiled/Release/basic_notex.frag.c',
    'Shaders/Compiled/Release/basic.vert.c',
    'Shaders/Compiled/Release/cs_tex_warp.comp.c',
    'Shaders/Compiled/Release/postprocess.frag.c',
    'Shaders/Compiled/Release/postprocess.vert.c',
    'Shaders/Compiled/Release/screen_effects_10bit.comp.c',
    'Shaders/Compiled/Release/screen_effects_10bit_scale.comp.c',
    'Shaders/Compiled/Release/screen_effects_10bit_scale_sops.comp.c',
    'Shaders/Compiled/Release/screen_effects_8bit.comp.c',
    'Shaders/Compiled/Release/screen_effects_8bit_scale.comp.c',
    'Shaders/Compiled/Release/screen_effects_8bit_scale_sops.comp.c',
    'Shaders/Compiled/Release/showtris.frag.c',
    'Shaders/Compiled/Release/showtris.vert.c',
    'Shaders/Compiled/Release/sky_box.frag.c',
    'Shaders/Compiled/Release/sky_layer.frag.c',
    'Shaders/Compiled/Release/sky_layer.vert.c',
    'Shaders/Compiled/Release/update_lightmap.comp.c',
    'Shaders/Compiled/Release/world.frag.c',
    'Shaders/Compiled/Release/world.vert.c',
]

cc = meson.get_compiler('c')
deps = [
    cc.find_library('m', required : false),
    cc.find_library('dl', required : false),
    dependency('threads'),
    dependency('sdl2'),
]

if build_machine.system() == 'darwin'
    deps += cc.find_library('MoltenVK', required : true)
else
    deps += dependency('vulkan')
endif

if get_option('mp3_lib') == 'mad'
    mp3_dep = dependency('mad', required : get_option('use_codec_mp3'))
    if mp3_dep.found()
        srcs += [ 'Quake/snd_mp3.c', 'Quake/snd_mp3tag.c' ]
        deps += mp3_dep
        cflags += '-DUSE_CODEC_MP3'
    endif
else
    mp3_dep = dependency('libmpg123', required : get_option('use_codec_mp3'))
    if mp3_dep.found()
        srcs += [ 'Quake/snd_mpg123.c', 'Quake/snd_mp3tag.c' ]
        deps += mp3_dep
        cflags += '-DUSE_CODEC_MP3'
    endif
endif

flac_dep = dependency('flac', required : get_option('use_codec_flac'))
if flac_dep.found()
    srcs += 'Quake/snd_flac.c'
    deps += flac_dep
    cflags += '-DUSE_CODEC_FLAC'
endif

ogg_dep = dependency('ogg', required : get_option('use_codec_vorbis'))
if get_option('vorbis_lib') == 'vorbis'
    vorbisfile_dep = dependency('vorbisfile', required : get_option('use_codec_vorbis'))
    vorbis_dep = dependency('vorbis', required : get_option('use_codec_vorbis'))
    if ogg_dep.found() and vorbisfile_dep.found() and vorbis_dep.found()
        srcs += 'Quake/snd_vorbis.c'
        deps += [ ogg_dep, vorbisfile_dep, vorbis_dep ]
        cflags += '-DUSE_CODEC_VORBIS'
    endif
else
    vorbis_dep = dependency('vorbisidec', required : get_option('use_codec_vorbis'))
    if ogg_dep.found() and vorbis_dep.found()
        srcs += 'Quake/snd_vorbis.c'
        deps += [ ogg_dep, vorbis_dep ]
        cflags += [ '-DUSE_CODEC_VORBIS', '-DVORBIS_USE_TREMOR' ]
    endif
endif

opus_dep = dependency('opus', required : get_option('use_codec_opus'))
opusfile_dep = dependency('opusfile', required : get_option('use_codec_opus'))
if opus_dep.found() and opusfile_dep.found()
    srcs += 'Quake/snd_opus.c'
    deps += [ opus_dep, opusfile_dep ]
    cflags += '-DUSE_CODEC_OPUS'
endif

executable('vkquake', srcs, dependencies : deps, c_args : cflags)
