<!--
SPDX-FileCopyrightText: 2026 SombrAbsol

SPDX-License-Identifier: MIT
-->

# kvrom2json
<a href="https://github.com/SombrAbsol/kvrom2json/actions/workflows/ci.yml"><img src="https://github.com/SombrAbsol/kvrom2json/actions/workflows/ci.yml/badge.svg" alt="CI"></a>
<a href="https://opensource.org/license/mit"><img src="https://img.shields.io/badge/license-MIT-blue" alt="License: MIT (Expat)"></a>

KVROM text file converter for *Pokémon Trading Card Game Pocket*.

KVROM files are used to store text in this game. This tool allows you to convert KVROM files to JSON. You can [download the latest build](#download) or [build the program from source](#building).

For more information about the KVROM format, see [the documentation](/docs/kvrom.md).

## Download
|        | Linux | macOS | Windows |
| ------ | ----- | ----- | ------- |
| Latest | [Download](https://github.com/SombrAbsol/kvrom2json/releases/download/latest/kvrom2json-linux.zip) | [Download](https://github.com/SombrAbsol/kvrom2json/releases/download/latest/kvrom2json-macos.zip) | [Download](https://github.com/SombrAbsol/kvrom2json/releases/download/latest/kvrom2json-windows.zip) |

## Usage
### Getting the game assets
> [!IMPORTANT]
> You will need to [download aladump](https://github.com/SombrAbsol/aladump/releases/latest) and follow the [related instructions](https://github.com/SombrAbsol/aladump#getting-the-game-assets) there to decrypt *Pokémon Trading Card Game Pocket*'s assets. These instructions assume you use the Android release of the game.

### Getting the KVROM files
Once you have decrypted the game's assets, you can find the text bundles in the `Common/Locale/` directory, each named after a locale. Use a Unity asset extraction tool of your choice, like [AssetStudioModGUI](https://github.com/aelurum/AssetStudio/releases/latest) (Windows only) or [AssetRipper](https://github.com/assetripper/assetripper) (Linux, macOS, Windows) to load these bundles and extract the KVROM files they contain.

> [!TIP]
> If you use AssetStudioMod, it is easier to browse through exported assets by grouping them by container path and use the asset name as file name format (see `Options > Export options`).

### Running kvrom2json
> [!IMPORTANT]
> kvrom2json is a command-line program and must be run in a terminal.

* To convert a KVROM file to a JSON file, run `kvrom2json <infile>`
* To convert KVROM files in a directory to JSON files, run `kvrom2json <indir>`

The output file will be located next to the input file with the same name, replacing the original extension with `.json`.

By default, the entries in the output file are listed in the order in which they appear in the input KVROM file. If you want to sort them alphabetically by key name, run `kvrom2json -s <in>` or `kvrom2json --sort <in>` (with `<in>` being an input file or directory).

> [!TIP]
> Directory processing is not recursive. To process extracted locale folders in one go, run one of the following commands:
> * Linux/macOS: `for d in */; do ./kvrom2json "${d%/}"; done`
> * Windows: `for /D %D in (*) do kvrom2json.exe "%D"`
> This is especially useful if you use AssetRipper or followed the previous AssetStudioMod tip.

## Building
### Dependencies
* `clang` or `gcc`
* `make`
* `zlib` (preferred over `zlib-static` which is used only for static builds)

### Steps
1. If you don't already have them, install the dependencies
2. Clone this repository by running `git clone https://github.com/SombrAbsol/kvrom2json`, or [download the ZIP archive](https://github.com/SombrAbsol/kvrom2json/archive/refs/heads/main.zip) and extract it
3. Go to the repository directory and build the executable by running `make`

> [!TIP]
> Running `make` or `make release` will generate a release build. The downloadable releases are static builds generated using this recipe. If you want to generate a native or a debug build, run `make native` or `make debug`. Native builds are optimized for your specific CPU for better performance but may not be compatible with other systems, while debug builds include debugging symbols that help diagnose issues but run slower.
>
> Unix-like operating systems (such as Linux and macOS) can run `sudo make install` to install kvrom2json system-wide, preferably after building a release or native build. Use `sudo make uninstall` to remove it.
>
> If you need to rebuild the program, run `make clean` or delete the `build` directory.

## Credits
* kvrom2json by [SombrAbsol](https://github.com/SombrAbsol)
* Based on a Python program made by [LukeFZ](https://github.com/LukeFZ)

## License
kvrom2json is free software. You can redistribute it and/or modify it under the [terms of the Expat License](/LICENSE) as published by the Massachusetts Institute of Technology.
