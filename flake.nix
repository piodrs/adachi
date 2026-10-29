{
  description = "Adachi minimal X11 tilling window manager";

  inputs.nixpkgs.url = "github:nixos/nixpkgs/26.05";

  outputs =
    { nixpkgs, ... }:
    let
      system = "x86_64-linux";
      pkgs = nixpkgs.legacyPackages.${system};

      mkAdachi =
        {
          lib,
          stdenv,
          libx11,
          configFile ? null,
        }:
        stdenv.mkDerivation {
          pname = "adachi";
          version = "0.2.2";

          src = lib.fileset.toSource {
            root = ./.;
            fileset = lib.fileset.unions [
              ./main.c
              ./wm.c
              ./wm.h
              ./thirdparty/axiom.h
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
            description = "Minimal tilling window manager for X11, inspired by Ratpoison";
            license = lib.licenses.mit;
            mainProgram = "adachi";
            platforms = lib.platforms.linux;
          };
        };

      adachi = pkgs.callPackage mkAdachi { };
    in
    {
      packages.${system}.default = adachi;

      overlays.default = final: _prev: {
        adachi = final.callPackage mkAdachi { };
      };

      apps.${system}.default = {
        type = "app";
        program = nixpkgs.lib.getExe adachi;
        meta.description = "Run the Adachi window manager";
      };

      checks.${system}.build = adachi;

      formatter.${system} = pkgs.nixfmt;

      devShells.${system} = {
        default = pkgs.mkShell {
          inputsFrom = [ adachi ];
          packages = with pkgs; [
            gcc
            gdb
            xorg-server
            xterm
          ];
        };
      };
    };
}
