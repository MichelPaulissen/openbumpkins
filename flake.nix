{
  description = "Beasts & Bumpkins reverse engineering and native Linux C port";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    bb-patch = {
      url = "github:bb-patch/bb-patch/f2312876412ee356f7496fd66d72f88364b73bbe";
      flake = false;
    };
  };

  outputs =
    { nixpkgs, bb-patch, ... }:
    let
      system = "x86_64-linux";
      pkgs = nixpkgs.legacyPackages.${system};
      pkgs32 = pkgs.pkgsi686Linux;
      python = pkgs.python3.withPackages (
        ps: with ps; [
          pefile
          capstone
          unicorn
          pyghidra
          pillow
          pytest
          python-xlib
        ]
      );

      tools = with pkgs; [
        # Static analysis, headless decompilation, and runtime debugging.
        ghidra
        openjdk21
        binutils
        radare2
        gdb
        strace
        ltrace
        valgrind
        wineWow64Packages.stable
        xvfb-run
        python

        # Native C builds, formatting, and the upstream Go patcher.
        cmake
        ninja
        pkg-config
        clang-tools
        nixfmt
        go
        git

        # Tools for user-supplied installation media.
        file
        unzip
        libarchive
        bchunk
        cdrkit
        cabextract
      ];

      environment = {
        GHIDRA_INSTALL_DIR = "${pkgs.ghidra}/lib/ghidra";
        BB_PATCH_SOURCE = "${bb-patch}";
        # Keep runtime state local; entering the shell does not launch Wine.
        shellHook = ''
          export BUMPKINS_ROOT="$(git rev-parse --show-toplevel)"
          export WINEPREFIX="$BUMPKINS_ROOT/.wine"
        '';
      };
    in
    {
      devShells.${system} = {
        default = pkgs.mkShell (
          environment
          // {
            name = "bumpkins-linux";
            packages = tools;
            buildInputs = with pkgs; [
              SDL2
              SDL2_image
              SDL2_ttf
            ];
          }
        );

        # 32-bit PE helpers/hooks for calls into the original game under Wine.
        # Its cross stdenv supplies CC without changing the native shell's CC.
        windows = pkgs.pkgsCross.mingw32.mkShell (
          environment
          // {
            name = "bumpkins-windows-bridge";
            nativeBuildInputs = tools;
            GOOS = "windows";
            GOARCH = "386";
            CGO_ENABLED = "1";
          }
        );

        # Native C runtime with the temporary i386 compatibility ABI.
        native = pkgs32.mkShell (
          environment
          // {
            name = "bumpkins-native-i386";
            nativeBuildInputs = tools;
            buildInputs = [ pkgs32.SDL2 ];
          }
        );
      };

      formatter.${system} = pkgs.nixfmt;
    };
}
