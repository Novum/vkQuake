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
    vulkan-headers
    glslang
    spirv-tools
  ];
}
