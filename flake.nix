{
  description = "flake for rust";

  outputs =
    {
      nixpkgs,
      ...
    }:
    let
      forAllSystems =
        f:
        nixpkgs.lib.genAttrs nixpkgs.lib.systems.flakeExposed (system: f nixpkgs.legacyPackages.${system});
    in
    {
      packages = forAllSystems (pkgs: {
        default = pkgs.stdenv.mkDerivation {
          name = "stickyring";
          version = "0.1.0";

          src = ./src;
          buildInputs = [
            pkgs.liburing
          ];
          buildPhase = ''
            runHook preBuild
            $CC main.c -o stickyring -luring -O2 -s
            runHook postBuild
          '';

          installPhase = ''
            mkdir -p $out/bin
            mv stickyring $out/bin/stickyring
          '';
            
        };
      });

      devShells = forAllSystems (pkgs: {
        default = pkgs.mkShell {
          buildInputs = with pkgs; [
            stdenv.cc.cc.lib
            clang-tools
            liburing
            clang
            llvmPackages.lldb
            just
          ];
        };

      });

    };
}
