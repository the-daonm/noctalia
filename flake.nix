{
  description = "Noctalia - A lightweight Wayland shell and bar";

  inputs = {
    nixpkgs.url = "https://channels.nixos.org/nixos-unstable/nixexprs.tar.xz";
  };

  outputs =
    { self, nixpkgs }:
    let
      inherit (builtins) substring;
      inherit (nixpkgs.lib) concatStringsSep genAttrs getExe;

      systems = [
        "x86_64-linux"
        "aarch64-linux"
      ];

      forEachSystem =
        perSystem:
        genAttrs systems (
          system:
          let
            pkgs = nixpkgs.legacyPackages.${system};
          in
          perSystem { inherit pkgs system; }
        );

      mkDate =
        longDate:
        concatStringsSep "-" [
          (substring 0 4 longDate)
          (substring 4 2 longDate)
          (substring 6 2 longDate)
        ];

      shortRev = self.shortRev or "dirty";
      version = mkDate (self.lastModifiedDate or "19700101") + "_" + shortRev;
    in
    {
      overlays.default = final: prev: {
        default = (final.callPackage ./nix/package.nix { }) {
          inherit version shortRev;
        };
      };

      packages = forEachSystem (
        { pkgs, ... }:
        {
          default = (pkgs.callPackage ./nix/package.nix { }) {
            inherit version shortRev;
          };
        }
      );

      devShells = forEachSystem (
        { pkgs, system }:
        {
          default = pkgs.callPackage ./nix/devshell.nix {
            noctalia = self.packages.${system}.default;
          };
        }
      );

      apps = forEachSystem (
        { system, ... }:
        {
          default = {
            type = "app";
            program = getExe self.packages.${system}.default;
          };
        }
      );

      homeModules.default =
        { pkgs, lib, ... }:
        {
          imports = [ ./nix/home-module.nix ];
          programs.noctalia.package = lib.mkDefault self.packages.${pkgs.stdenv.hostPlatform.system}.default;
          _class = "homeManager";
        };

      hjemModules.default =
        { pkgs, lib, ... }:
        {
          imports = [ ./nix/hjem-module.nix ];
          programs.noctalia.package = lib.mkDefault self.packages.${pkgs.stdenv.hostPlatform.system}.default;
          _class = "hjem";
        };
    };
}
