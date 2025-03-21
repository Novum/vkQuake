{ pkgs ? import <nixpkgs> {} }:

pkgs.mkShell {
  buildInputs = with pkgs;[
    git
    meson
    flac
    glibc
    libGL
    mpg123
    libvorbis
    xorg.libX11
    SDL2
    vulkan-loader
    glslang
    spirv-tools
    pkg-config
    libgcc
    glslang
    cmake
    opusfile
    ninja
  ];
}
