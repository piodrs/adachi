{
  description = "Adachi minimal X11 window manager";

  inputs.nixpkgs.url = "github:nixos/nixpkgs/26.05";

  outputs =
    { self, nixpkgs, ... }:
    let
      systems = [
        "x86_64-linux"
      ];
      forAllSystems = nixpkgs.lib.genAttrs systems;

      mkAdachi =
        {
          lib,
          stdenv,
          libx11,
          configFile ? null,
        }:
        stdenv.mkDerivation {
          pname = "adachi";
          version = "0.2.0";

          src = lib.fileset.toSource {
            root = ./.;
            fileset = lib.fileset.unions [
              ./main.c
              ./wm.c
              ./wm.h
              ./config.h
              ./build.sh
            ];
          };

          buildInputs = [ libx11 ];

          postPatch = lib.optionalString (configFile != null) ''
            cp ${configFile} config.h
          '';

          dontConfigure = true;

          buildPhase = ''
            runHook preBuild
            sh ./build.sh
            runHook postBuild
          '';

          installPhase = ''
            runHook preInstall
            install -Dm755 adachi $out/bin/adachi
            runHook postInstall
          '';

          meta = {
            description = "Minimal fullscreen window manager for X11, inspired by Ratpoison";
            license = lib.licenses.mit;
            mainProgram = "adachi";
            platforms = lib.platforms.linux;
          };
        };
    in
    {
      packages = forAllSystems (system: {
        default = nixpkgs.legacyPackages.${system}.callPackage mkAdachi { };
      });

      overlays.default = final: _prev: {
        adachi = final.callPackage mkAdachi { };
      };

      apps = forAllSystems (system: {
        default = {
          type = "app";
          program = nixpkgs.lib.getExe self.packages.${system}.default;
          meta.description = "Run the Adachi window manager";
        };
      });

      checks = forAllSystems (system: {
        build = self.packages.${system}.default;
      });

      formatter = forAllSystems (system: nixpkgs.legacyPackages.${system}.nixfmt);

      devShells = forAllSystems (
        system:
        let
          pkgs = nixpkgs.legacyPackages.${system};
        in
        {
          default = pkgs.mkShell {
            inputsFrom = [ self.packages.${system}.default ];
            packages = with pkgs; [
              gdb
              xorg-server
              xterm
            ];
          };
        }
      );
    };
}
