with import <nixpkgs> {};
pkgs.mkShell {
  nativeBuildInputs = with pkgs; [
    glfw
    libGL
    clang-tools
    python3
    pyright
  ];
}
