{
  description = "flake for github:lavafroth/stickyring";

  outputs =
    {
      self,
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

      nixosModules.default =
        {
          config,
          lib,
          pkgs,
          ...
        }:
        let
          cfg = config.services.stickyring;
        in
        {
          options.services.stickyring = {
            enable = lib.mkEnableOption "Stickyring sticky keys service";
            # timeout = lib.mkOption {
            #   type = lib.types.int;
            #   description = "lock a key if pressed twice within this time window";
            #   default = 500;
            # };
            # modifiers = lib.mkOption {
            #   type = lib.types.str;
            #   description = "the modifiers to register and augment";
            #   default = "leftshift,leftctrl,compose,leftmeta,fn";
            # };
            # device = lib.mkOption {
            #   type = lib.types.str;
            #   description = "the keyboard device to listen on";
            #   default = "autodetect";
            # };
            # clearAllWithEscape = lib.mkOption {
            #   type = lib.types.bool;
            #   description = "clear all latched and locked keys by pressing escape";
            #   default = true;
            # };
            # sharedMemory = lib.mkOption {
            #   type = lib.types.bool;
            #   description = "write to a file in /dev/shm to be watched by on screen indicators";
            #   default = false;
            # };
            # touchpad.enable = lib.mkOption {
            #   type = lib.types.bool;
            #   description = "clear latched and locked keys when touchpad is clicked";
            #   default = true;
            # };
            # touchpad.timeout = lib.mkOption {
            #   type = lib.types.int;
            #   description = "how long a touchpad can dwell in the touched state before considering the input as a click";
            #   default = 400;
            # };
            # touchpad.slop = lib.mkOption {
            #   type = lib.types.int;
            #   description = "register a touch as a tap even if the finger moves slightly by this amount of touchpad units";
            #   default = 50;
            # };
          };

          config = lib.mkIf cfg.enable {

            systemd.services.stickyring = {
              description = "stickyring sticky keys service";
              wantedBy = [ "multi-user.target" ];
              serviceConfig = {

                ExecStart = "${self.packages.${pkgs.system}.default}/bin/stickyring";
                # ExecStart = "${self.packages.${pkgs.system}.default}/bin/stickyring ${
                  # let configContents = lib.generators.toINIWithGlobalSection { } {

                  #   globalSection = {
                  #     timeout = cfg.timeout;
                  #     modifiers = cfg.modifiers;
                  #     device = cfg.device;
                  #     clear_all_with_escape = cfg.clearAllWithEscape;
                  #     shared_memory = cfg.sharedMemory;
                  #   };

                  #     sections = {
                  #       touchpad = cfg.touchpad;
                  #     };

                  # }; in
                  # pkgs.writeText "config.ini" configContents
                # }";

                Type = "exec";
              };
            };
          };
        };

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
