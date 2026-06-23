{
  pkgs,
  ...
}:

{
  packages = [
    pkgs.gnumake
    pkgs.cmake
    pkgs.pkg-config
    pkgs.gcc
    pkgs.sqlite
    pkgs.ninja
  ];

  languages.c.enable = true;
}
