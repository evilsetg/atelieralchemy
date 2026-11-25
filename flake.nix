{
  description = "Atelier Alchemy - Skyrim SKSE plugin dev shell (clang-cl cross-compile)";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs = { self, nixpkgs }:
    let
      system = "x86_64-linux";
      pkgs = nixpkgs.legacyPackages.${system};
      llvm = pkgs.llvmPackages_20;
    in {
      devShells.${system}.default = pkgs.mkShell {
        packages = [
          llvm.clang-unwrapped # provides clang-cl
          llvm.lld             # provides lld-link
          llvm.llvm            # llvm-rc, llvm-mt, llvm-lib
          pkgs.cmake
          pkgs.ninja
          pkgs.xwin
          pkgs.git
          pkgs.python3
          pkgs.p7zip
        ];
        # Nix splits clang's builtin headers (cpuid.h, intrinsics...) into the `lib`
        # output, so clang-cl can't find them relative to its binary.
        CLANG_RESOURCE_DIR = "${llvm.clang-unwrapped.lib}/lib/clang/${pkgs.lib.versions.major llvm.clang-unwrapped.version}";
        shellHook = ''
          export XWIN_SYSROOT="''${XWIN_SYSROOT:-$PWD/.xwin/sysroot}"
          if [ ! -d "$XWIN_SYSROOT" ]; then
            echo "Windows SDK/CRT sysroot missing. Run: ./scripts/setup-xwin.sh"
          fi
        '';
      };
    };
}
