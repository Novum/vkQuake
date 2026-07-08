project(
    'vkquake',
    'c',
    meson_version : '>= 1.3',
    default_options : ['c_std=gnu11,c11', 'buildtype=release', 'strip=false', 'debug=true', 'b_ndebug=if-release', 'warning_level=1', 'werror=true'])

cc = meson.get_compiler('c')

# Always build keeping frame pointers to get better backtraces
add_project_arguments(cc.get_supported_arguments('-fno-omit-frame-pointer', '/Oy-'), language: 'c')

if host_machine.system() == 'windows'
    foreach native : [true, false]
        add_project_arguments('-D_CRT_SECURE_NO_WARNINGS', '-D_CRT_NONSTDC_NO_DEPRECATE', '-D_WINSOCK_DEPRECATED_NO_WARNINGS', language : 'c', native : native)
    endforeach
endif

if host_machine.system() != 'windows'
    if cc.has_header_symbol('sched.h', 'CPU_ZERO', prefix: '#define _GNU_SOURCE')
        message('Info: Task affinity support enabled.')
        add_project_arguments('-D_GNU_SOURCE', language: 'c')
    else
        message('Warning: sched.h not found, disabling Task affinity support.')
        add_project_arguments('-DTASK_AFFINITY_NOT_AVAILABLE', language: 'c')
    endif
endif

shaders = [
    'Shaders/alias.vert',
    'Shaders/md5.vert',
    'Shaders/basic.vert',
    'Shaders/basic_alphatest.frag',
    'Shaders/basic_notex.frag',
    'Shaders/cs_tex_warp.comp',
    'Shaders/indirect.comp',
    'Shaders/indirect_clear.comp',
    'Shaders/postprocess.frag',
    'Shaders/postprocess.vert',
    'Shaders/showtris.frag',
    'Shaders/showtris.vert',
    'Shaders/sky_box.frag',
    'Shaders/sky_cube.frag',
    'Shaders/sky_cube.vert',
    'Shaders/sky_layer.frag',
    'Shaders/sky_layer.vert',
    'Shaders/world.vert',
    'Shaders/ray_debug.comp',
    'Shaders/mesh_interpolate.comp',
    'Shaders/skinning.comp',
]

# Prefer SDL3 over SDL2; -Duse_sdl3=enabled requires SDL3, disabled forces SDL2
use_sdl3_opt = get_option('use_sdl3')
use_sdl3 = false
if host_machine.system() == 'windows'
    if use_sdl3_opt.disabled()
        error('SDL2 is not supported on Windows')
    endif
    # use the bundled SDL3 like the other Windows builds do
    sdl_dep = declare_dependency(
        include_directories : include_directories('Windows/SDL3/include'),
        dependencies : cc.find_library('SDL3', dirs : meson.project_source_root() / 'Windows/SDL3/lib64'))
    use_sdl3 = true
else
    if not use_sdl3_opt.disabled()
        sdl_dep = dependency('sdl3', required : use_sdl3_opt.enabled())
        use_sdl3 = sdl_dep.found()
    endif
endif
if use_sdl3
    add_project_arguments('-DUSE_SDL3', language: 'c')
    message('Info: Using SDL3.')
else
    sdl_dep = dependency('sdl2')
    message('Info: Using SDL2.')
endif

glslang = find_program('glslangValidator')
spirv_opt = find_program('spirv-opt')
run_test_canonicalize_ids_spirv_opt = run_command(spirv_opt, '-h', check : false)
has_canonicalize_ids_spirv_opt = (run_test_canonicalize_ids_spirv_opt.stdout().strip().contains('--canonicalize-ids'))
bintoc = executable('bintoc', ['Shaders/bintoc.c'], native: true)
mkpak = executable('mkpak', ['Misc/vq_pak/mkpak.c'], native: true)

if (build_machine.system() == 'darwin') or get_option('buildtype').startswith('debug')
    spirv_opt_args = [] # MoltenVK spirv-cross has a bug that breaks optimized shaders
else
    if not has_canonicalize_ids_spirv_opt
        error('spirv-opt does not support --canonicalize-ids, a newer Vulkan SDK is required')
    endif
    spirv_opt_args = ['-Os', '--canonicalize-ids', '--strip-debug']
endif

glslang_args = ['-V', '--quiet']
if get_option('buildtype').startswith('debug')
    glslang_args += '-g'
endif

vq_pak_path = join_paths(meson.project_source_root(), 'Misc', 'vq_pak')
mkpak_toc_file = join_paths(vq_pak_path, 'vq_pak_contents.txt')

mkpak_command = [mkpak, '@OUTPUT@', vq_pak_path, mkpak_toc_file, '@DEPFILE@']

run_mkpak = custom_target('run-mkpak',
  output : 'vkquake.pak',
  depfile : 'vkquake.pak.d',
  command: mkpak_command)

# Write the embedded pak into the build dir so Meson can track it.
bintoc_compress_command = [bintoc, '-c', '@INPUT@', 'vkquake.pak', '@OUTPUT@']

run_generate_embedded_pak = custom_target('run-embed_vkquake_pak',
							output : 'embedded_pak.c',
							input : run_mkpak,
							command : bintoc_compress_command)

# shader sources compiled multiple times with different preprocessor defines
shader_variants = [
    ['Shaders/basic.frag', 'basic.frag', []],
    ['Shaders/basic.frag', 'basic_oit.frag', ['-DWBOIT=1']],
    ['Shaders/basic.frag', 'basic_mboit_moment.frag', ['-DMBOIT=1']],
    ['Shaders/basic.frag', 'basic_mboit_composite.frag', ['-DMBOIT=1', '-DMBOIT_COMPOSITE=1']],
    ['Shaders/basic.frag', 'basic_mboit_composite_msaa.frag', ['-DMBOIT=1', '-DMBOIT_COMPOSITE=1', '-DMSAA=1']],
    ['Shaders/world.frag', 'world.frag', []],
    ['Shaders/world.frag', 'world_oit.frag', ['-DWBOIT=1']],
    ['Shaders/world.frag', 'world_mboit_moment.frag', ['-DMBOIT=1']],
    ['Shaders/world.frag', 'world_mboit_composite.frag', ['-DMBOIT=1', '-DMBOIT_COMPOSITE=1']],
    ['Shaders/world.frag', 'world_mboit_composite_msaa.frag', ['-DMBOIT=1', '-DMBOIT_COMPOSITE=1', '-DMSAA=1']],
    ['Shaders/alias.frag', 'alias.frag', []],
    ['Shaders/alias.frag', 'alias_alphatest.frag', ['-DALIAS_ALPHA_TEST=1']],
    ['Shaders/alias.frag', 'alias_oit.frag', ['-DWBOIT=1']],
    ['Shaders/alias.frag', 'alias_alphatest_oit.frag', ['-DALIAS_ALPHA_TEST=1', '-DWBOIT=1']],
    ['Shaders/alias.frag', 'alias_mboit_moment.frag', ['-DMBOIT=1']],
    ['Shaders/alias.frag', 'alias_alphatest_mboit_moment.frag', ['-DALIAS_ALPHA_TEST=1', '-DMBOIT=1']],
    ['Shaders/alias.frag', 'alias_mboit_composite.frag', ['-DMBOIT=1', '-DMBOIT_COMPOSITE=1']],
    ['Shaders/alias.frag', 'alias_alphatest_mboit_composite.frag', ['-DALIAS_ALPHA_TEST=1', '-DMBOIT=1', '-DMBOIT_COMPOSITE=1']],
    ['Shaders/alias.frag', 'alias_mboit_composite_msaa.frag', ['-DMBOIT=1', '-DMBOIT_COMPOSITE=1', '-DMSAA=1']],
    ['Shaders/alias.frag', 'alias_alphatest_mboit_composite_msaa.frag', ['-DALIAS_ALPHA_TEST=1', '-DMBOIT=1', '-DMBOIT_COMPOSITE=1', '-DMSAA=1']],
    ['Shaders/alias.frag', 'md5_mboit_composite.frag', ['-DMBOIT=1', '-DMBOIT_COMPOSITE=1', '-DMBOIT_INPUT_SET=4']],
    ['Shaders/alias.frag', 'md5_alphatest_mboit_composite.frag', ['-DALIAS_ALPHA_TEST=1', '-DMBOIT=1', '-DMBOIT_COMPOSITE=1', '-DMBOIT_INPUT_SET=4']],
    ['Shaders/alias.frag', 'md5_mboit_composite_msaa.frag', ['-DMBOIT=1', '-DMBOIT_COMPOSITE=1', '-DMSAA=1', '-DMBOIT_INPUT_SET=4']],
    ['Shaders/alias.frag', 'md5_alphatest_mboit_composite_msaa.frag', ['-DALIAS_ALPHA_TEST=1', '-DMBOIT=1', '-DMBOIT_COMPOSITE=1', '-DMSAA=1', '-DMBOIT_INPUT_SET=4']],
    ['Shaders/wboit_resolve.frag', 'wboit_resolve.frag', []],
    ['Shaders/wboit_resolve.frag', 'wboit_resolve_msaa.frag', ['-DMSAA=1']],
    ['Shaders/mboit_resolve.frag', 'mboit_resolve.frag', []],
    ['Shaders/mboit_resolve.frag', 'mboit_resolve_msaa.frag', ['-DMSAA=1']],
    ['Shaders/screen_effects.comp', 'screen_effects_8bit.comp', []],
    ['Shaders/screen_effects.comp', 'screen_effects_8bit_scale.comp', ['-DSCALING=1']],
    ['Shaders/screen_effects.comp', 'screen_effects_8bit_scale_sops.comp', ['--target-env', 'vulkan1.1', '-DSCALING=1', '-DUSE_SUBGROUP_OPS=1']],
    ['Shaders/screen_effects.comp', 'screen_effects_10bit.comp', ['-DUSE_10BIT=1']],
    ['Shaders/screen_effects.comp', 'screen_effects_10bit_scale.comp', ['-DUSE_10BIT=1', '-DSCALING=1']],
    ['Shaders/screen_effects.comp', 'screen_effects_10bit_scale_sops.comp', ['--target-env', 'vulkan1.1', '-DUSE_10BIT=1', '-DSCALING=1', '-DUSE_SUBGROUP_OPS=1']],
    ['Shaders/update_lightmap.comp', 'update_lightmap_8bit.comp', []],
    ['Shaders/update_lightmap.comp', 'update_lightmap_8bit_rt.comp', ['-DRAY_QUERIES=1']],
    ['Shaders/update_lightmap.comp', 'update_lightmap_10bit.comp', ['-DUSE_10BIT=1']],
    ['Shaders/update_lightmap.comp', 'update_lightmap_10bit_rt.comp', ['-DUSE_10BIT=1', '-DRAY_QUERIES=1']],
]

fs = import('fs')

shader_jobs = []
foreach shader : shaders
    target_env = shader.contains('sops') ? ['--target-env', 'vulkan1.1'] : []
    shader_jobs += [[shader, fs.name(shader), target_env]]
endforeach
foreach variant : shader_variants
    shader_jobs += [[variant[0], variant[1], variant[2]]]
endforeach

shaders_c = []
foreach job : shader_jobs
    source = job[0]
    name = job[1]
    args = glslang_args + job[2]
    if spirv_opt_args.length() > 0
        spv = custom_target(name.underscorify() + '_raw_spv',
            input : source,
            output : name + '.raw.spv',
            depfile : name + '.deps',
            command : [glslang] + args + ['-o', '@OUTPUT@', '@INPUT@', '--depfile', '@DEPFILE@'])
        spv = custom_target(name.underscorify() + '_spv',
            input : spv,
            output : name + '.spv',
            command : [spirv_opt] + spirv_opt_args + ['@INPUT@', '-o', '@OUTPUT@'])
    else
        spv = custom_target(name.underscorify() + '_spv',
            input : source,
            output : name + '.spv',
            depfile : name + '.deps',
            command : [glslang] + args + ['-o', '@OUTPUT@', '@INPUT@', '--depfile', '@DEPFILE@'])
    endif
    shaders_c += custom_target(name.underscorify() + '_c',
        input : spv,
        output : name + '.c',
        command : [bintoc, '@INPUT@', name + '_spv', '@OUTPUT@'])
endforeach

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
    'Quake/json.c',
    'Quake/in_sdl.c',
    'Quake/in_sdl2.c',
    'Quake/in_sdl3.c',
    'Quake/keys.c',
    'Quake/main_sdl.c',
    'Quake/mathlib.c',
    'Quake/mdfour.c',
    'Quake/mem.c',
    'Quake/menu.c',
    'Quake/net_dgrm.c',
    'Quake/net_loop.c',
    'Quake/net_main.c',
    'Quake/palette.c',
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
    'Quake/snd_sdl3.c',
    'Quake/snd_umx.c',
    'Quake/snd_wave.c',
    'Quake/steam.c',
    'Quake/strlcat.c',
    'Quake/strlcpy.c',
    'Quake/sv_main.c',
    'Quake/sv_move.c',
    'Quake/sv_phys.c',
    'Quake/sv_user.c',
	'Quake/sys_sdl.c',
    'Quake/tasks.c',
    'Quake/view.c',
    'Quake/wad.c',
    'Quake/world.c',
	'Quake/hash_map.c',
	 run_generate_embedded_pak,
]

if host_machine.system() == 'windows'
    srcs += [
        'Quake/net_win.c',
        'Quake/net_wins.c',
        'Quake/net_wipx.c',
        'Quake/pl_win.c',
        'Quake/sys_sdl_win.c',
    ]
    srcs += import('windows').compile_resources(
        'Windows/vkQuake.rc',
        # the \xA9 copyright escape is Windows-1252; llvm-rc refuses 8 bit
        # codepoints without an explicit codepage. gcc builds use windres,
        # which takes no /C option
        args : cc.get_id() == 'gcc' ? [] : ['/C', '1252'],
        include_directories : include_directories('Windows', 'Quake'),
        depend_files : ['Windows/vkQuake.ico'])
else
    srcs += [
        'Quake/net_bsd.c',
        'Quake/net_udp.c',
        'Quake/pl_linux.c',
        'Quake/sys_sdl_unix.c',
    ]
endif

cflags = cc.get_supported_arguments('-Wno-trigraphs', '-Wno-microsoft-enum-forward-reference') + ['-D_FILE_OFFSET_BITS=64']
if cc.get_id() == 'clang'
    # the stb single header libraries define their whole API static
    cflags += '-Wno-unused-function'
endif

deps = [
    cc.find_library('m', required : false),
    cc.find_library('dl', required : false),
    dependency('threads'),
    sdl_dep,
]

if host_machine.system() == 'windows'
    foreach syslib : ['ws2_32', 'winmm', 'dbghelp']
        deps += cc.find_library(syslib)
    endforeach
    import('fs').copyfile('Windows/SDL3/lib64/SDL3.dll')
endif

if build_machine.system() == 'darwin'
    molten_dirs = []
    if import('fs').exists('/opt/homebrew/lib/libMoltenVK.dylib')
        molten_dirs += '/opt/homebrew/lib'
    endif
    deps += cc.find_library('MoltenVK', required : true, dirs : molten_dirs)
else
    deps += dependency('vulkan')
endif

if get_option('use_codec_wave').enabled()
    cflags += '-DUSE_CODEC_WAVE'
endif

if host_machine.system() == 'windows'
    # the bundled codec libraries, like the other Windows builds use
    codec_inc = include_directories('Windows/codecs/include')
    codec_lib_dir = meson.project_source_root() / 'Windows/codecs/x64'
    codec_deps = {}
    foreach name, option : {
        'libmad' : 'use_codec_mp3',
        'libmpg123' : 'use_codec_mp3',
        'libFLAC' : 'use_codec_flac',
        'libogg' : 'use_codec_vorbis',
        'libvorbisfile' : 'use_codec_vorbis',
        'libvorbis' : 'use_codec_vorbis',
        'libopus' : 'use_codec_opus',
        'libopusfile' : 'use_codec_opus',
    }
        lib = cc.find_library(name, dirs : codec_lib_dir, required : get_option(option))
        codec_deps += {name : lib.found() ? declare_dependency(include_directories : codec_inc, dependencies : lib) : lib}
    endforeach
    foreach dll : ['libFLAC-8.dll', 'libmad-0.dll', 'libmpg123-0.dll', 'libogg-0.dll', 'libopus-0.dll', 'libopusfile-0.dll', 'libvorbis-0.dll', 'libvorbisfile-3.dll']
        import('fs').copyfile('Windows/codecs/x64' / dll)
    endforeach
    mad_dep = codec_deps['libmad']
    mpg123_dep = codec_deps['libmpg123']
    flac_dep = codec_deps['libFLAC']
    ogg_dep = codec_deps['libogg']
    vorbisfile_dep = codec_deps['libvorbisfile']
    vorbis_dep = codec_deps['libvorbis']
    tremor_dep = dependency('', required : false)
    opus_dep = codec_deps['libopus']
    opusfile_dep = codec_deps['libopusfile']
else
    if get_option('mp3_lib') == 'mad'
        mad_dep = dependency('mad', required : get_option('use_codec_mp3'))
    else
        mpg123_dep = dependency('libmpg123', required : get_option('use_codec_mp3'))
    endif
    flac_dep = dependency('flac', required : get_option('use_codec_flac'))
    ogg_dep = dependency('ogg', required : get_option('use_codec_vorbis'))
    if get_option('vorbis_lib') == 'vorbis'
        vorbisfile_dep = dependency('vorbisfile', required : get_option('use_codec_vorbis'))
        vorbis_dep = dependency('vorbis', required : get_option('use_codec_vorbis'))
    else
        tremor_dep = dependency('vorbisidec', required : get_option('use_codec_vorbis'))
    endif
    opus_dep = dependency('opus', required : get_option('use_codec_opus'))
    opusfile_dep = dependency('opusfile', required : get_option('use_codec_opus'))
endif

if get_option('mp3_lib') == 'mad'
    if mad_dep.found()
        srcs += [ 'Quake/snd_mp3.c', 'Quake/snd_mp3tag.c' ]
        deps += mad_dep
        cflags += '-DUSE_CODEC_MP3'
    endif
else
    if mpg123_dep.found()
        srcs += [ 'Quake/snd_mpg123.c', 'Quake/snd_mp3tag.c' ]
        deps += mpg123_dep
        cflags += '-DUSE_CODEC_MP3'
    endif
endif

if flac_dep.found()
    srcs += 'Quake/snd_flac.c'
    deps += flac_dep
    cflags += '-DUSE_CODEC_FLAC'
endif

if get_option('vorbis_lib') == 'vorbis'
    if ogg_dep.found() and vorbisfile_dep.found() and vorbis_dep.found()
        srcs += 'Quake/snd_vorbis.c'
        deps += [ ogg_dep, vorbisfile_dep, vorbis_dep ]
        cflags += '-DUSE_CODEC_VORBIS'
    endif
else
    if ogg_dep.found() and tremor_dep.found()
        srcs += 'Quake/snd_vorbis.c'
        deps += [ ogg_dep, tremor_dep ]
        cflags += [ '-DUSE_CODEC_VORBIS', '-DVORBIS_USE_TREMOR' ]
    endif
endif

if opus_dep.found() and opusfile_dep.found()
    srcs += 'Quake/snd_opus.c'
    deps += [ opus_dep, opusfile_dep ]
    cflags += '-DUSE_CODEC_OPUS'
endif

if get_option('buildtype').startswith('debug')
    cflags += '-D_DEBUG'
endif

if get_option('do_userdirs').enabled()
    cflags += '-DDO_USERDIRS=1'
endif

incdirs = [
    'Quake/mimalloc',
]

executable(
    'vkquake',
    [srcs, shaders_c],
    dependencies : deps,
    include_directories : incdirs,
    c_args : cflags,
    c_pch : 'Quake/quakedef.h',
    win_subsystem : 'windows')
