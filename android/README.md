# Mod Loader for Android (AML plugin)

A port of **Mod Loader** to *GTA San Andreas for Android 2.00* (ARMv7, `libGTASA.so`), packaged as a plugin
for [AML (Android Mod Loader)](https://github.com/RusJJ/AndroidModLoader).

Drop a mod in a folder, start the game, the mod is in. Same idea, same folder rules, same `modloader.ini`
as the Windows version.

> **Status: first version, never run on a real device.** Everything that can be checked without the game was
> checked (see [Verification](#verification)), but the hooks have not seen a phone yet. Expect to read
> `modloader.log` a few times. Please report what happens.

* [Install](#install) · [Mods](#making-mods) · [What works](#what-works-and-what-does-not) · [Build](#building)
  · [How it works](#how-it-works) · [Verification](#verification) · [Notes for upstream](#notes-for-upstream)

## Install

1. Have AML installed in the game (see its documentation) and copy `libmodloader.so` to the AML mods folder
   (normally `Android/data/com.rockstargames.gtasa/mods/`).
2. Create the mods in `Android/data/com.rockstargames.gtasa/files/modloader/<Mod Name>/`
   (`files/` is the folder where the game already keeps its loose files).
3. Start the game. Look at `files/modloader/modloader.log`, it tells what was found and what was replaced.

The first start creates `modloader/modloader.ini` (profiles/priorities, same syntax as upstream, see
[`doc/Profiles.md`](../doc/Profiles.md)) and `modloader/.data/config.ini`.

## Making mods

A mod is a folder inside `modloader/`. Put the files anywhere inside it:

| File in the mod | What happens |
|---|---|
| `handling.cfg`, `american.gxt`, `gta.dat`, `fonts.dat`... any file the game reads from its folder | std.data: the game gets *your* file when it asks for it. Matched by path (`data/handling.cfg` replaces `data/handling.cfg`) or, if there is no such path, by file name alone. |
| `gta3.img`, other archives, whatever else the game reads with its file API | Same as above, the whole file is replaced. |
| `*.dff` `*.txd` `*.col` `*.ifp` `*.rrr` `*.scm` and binary (`bnry`) `*.ipl` | std.stream: replaces the entry with that name inside the game's IMG archives (`infernus.dff` replaces `infernus.dff` in `gta3.img`), the folder it is in does not matter. Names that are not in any archive are registered by the game as new entries (a new model still needs its IDE line). Entry names are at most 23 characters long. |
| text `*.ide` / `*.ipl` that are not replacements of a game file | std.data adds them to `gta.dat` (IDE lines before the first IPL, IPL lines at the end). |

Rules, as in the original:

* The mod with the highest priority wins when two mods provide the same thing (default priority is 50, see
  `modloader.ini`); with equal priorities the one that comes last in alphabetical order wins.
* Folders starting with `.` and the `_ignore` mod are skipped, files can be skipped with
  `[Profiles.Default.IgnoreFiles]`, all the *Ignore/Include/ExclusiveMods* lists work.
* Mod folders and files may have spaces and any case in their names.
* The file types of the std.stream row are matched by *name* against the IMG entries. If the game also has a loose file with
  that name (for instance `anim/ped.ifp`), put your file at the same path inside the mod (`anim/ped.ifp`) to replace the file.
  Scripts (`*.scm`) are the exception: `main.scm` is also found by name.

## What works, and what does not

| Upstream plugin | Android port |
|---|---|
| `std.data` (replace data files, add IDE/IPL) | ✔ whole file replacement, IDE/IPL addition. ✘ line-by-line merging of `handling.cfg`/`ped.dat`/... |
| `std.stream` (models, textures, collisions, animations, maps) | ✔ replace and add entries of the IMG archives. ✘ adding entirely new `.img` archives |
| `std.text` (`.gxt`/`.fxt`) | ◐ replacing `text/*.gxt` works (it is a normal file), merging `.fxt` does not |
| `std.asi`, `std.scm`, `std.fx`, `std.movies`, `std.sprites`, `std.bank`, `std.tracks`, `std.cleo`... | ✘ not ported (several make no sense on Android; AML mods are `.so`s) |
| Hot reloading / `AutoRefresh` | ✘ mods are read when the game starts |

The plugin interface is the C interface of the original (`include/modloader/modloader.h`), so more plugins can be added
the way the original does it: a `modloader_plugin_t` with `GetBehaviour`/`InstallFile`, registered in `aml_entry.cpp`.

## Troubleshooting

Everything the loader decides is written to `modloader/modloader.log` (the previous run is kept as `modloader.log.old`).
The first lines say which game library is running (path, size and an FNV-1a 64 hash; the library this port was analysed
with has 11707712 bytes and `236098b3ffc3c90a`), then one line per hook:

| Log line | Meaning |
|---|---|
| `... was already hooked by someone else` | Another loader (the Fastman92 plugin loader, another AML mod) hooked that function first. The port hooks on top of it, this is fine. |
| `unexpected code ... Hooking anyway` | The first bytes of the function are neither the analysed ones nor a hook. A different build of the game, most likely. The log prints what was found. Put `StrictChecks = true` in `modloader/.data/config.ini` to refuse instead. |
| `... not under an "/Android..." folder` | The game's files folder is outside of the external storage root, the mods cannot be handed to the game. |
| `X is not where it is exported: exported at A, the code of the game uses B` | Information only: the array was moved (by the Fastman92 loader); the port uses the live one. |
| `none of them is in CStreaming::ms_aInfoForModel` | A limit adjuster moved the streaming array; replacing entries of IMG archives is switched off, replacing files still works. |
| `config.ini (read): EnablePlugins=1 EnableData=1 EnableStream=1 ...` | The settings the loader is really using. `EnableStream=0` makes std.stream ignore every model/texture/collision/animation (and says so). |
| `Mod "X": N file(s) [dff=2, txd=2, ...]` and one `file -> plugin` line per file (the first 30) | What each plugin decided for the files of the mod. `no plugin wants this file` means it will never be used. |
| `trace: the game opens "DATA\\GTA.DAT" (area 0, access 0)  ->  /storage/.../modloader/...` | The first 600 different files the game opens (`TraceOpens`), the arrow shows the file of a mod that is served instead. If your file is never listed, the game never asks for that name: compare the names. |
| `std.stream: the game loads the directory of image N ("...")` | The IMG archives the game reads at start-up, followed by how many of their entries have a replacement. |
| `Done: N mod(s), M file(s), K handled by plugins` | `K = 0` means no plugin took the files: look at the lines above it. |

## Building

GitHub Actions does everything (`.github/workflows/android.yml`): unit tests on the runner, then the NDK build,
the zip with `libmodloader.so` is an artifact of the run (and of the release for `android-v*` tags).

By hand, with the Android NDK (r25 or newer):

```sh
cmake -S android -B build -G Ninja \
      -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake \
      -DANDROID_ABI=armeabi-v7a -DANDROID_PLATFORM=android-21 -DANDROID_STL=c++_static \
      -DCMAKE_BUILD_TYPE=Release
cmake --build build              # -> build/libmodloader.so
```

The AML SDK headers are downloaded at configure time (pinned commit, `-DAML_SDK_DIR=` to use a checkout of your own).
The unit tests build on any PC without the NDK:

```sh
cmake -S android -B build-host && cmake --build build-host && ctest --test-dir build-host --output-on-failure
```

## How it works

`libGTASA.so` exports its C++ symbols, so the port hooks and reads game data **by name** instead of by offset.

* **std.data** hooks `OS_FileOpen(OSFileDataArea, void**, const char*, OSFileAccessType)`, the single place every
  read of the game goes through (`CFileMgr::OpenFile`, the IMG archives, audio, texture databases...). When a mod
  provides the requested file, the same call is made with the name of the mod's file instead. Names that start with `/`
  are opened by `NvFOpen` relative to the external storage root, which is how a file of `modloader/` is handed over.
  If opening the replacement fails, the original name is used.
* **std.stream** packs the files of the mods into one IMG archive (`modloader/.data/cache/stream.img`, rewritten only when
  the files change) and hooks `CStreaming::LoadCdDirectory(const char*, int)`. The game keeps the *first* entry found for
  each name, so an extra archive cannot override the stock ones: after each stock archive is loaded its directory is read
  again and the streaming infos of the entries we replace are pointed to the generated archive (image index, position,
  size), and `ms_streamingBufferSize` is raised if needed. The archive is registered in the first free `ms_files` slot, the
  game then loads its directory by itself, which is what adds new models.
* The arrays of the game (`ms_files`, `ms_aInfoForModel`, `ms_streamingBufferSize`) are not taken from their exported symbol but from
  the GOT slot the code of `libGTASA.so` reads them from: the Fastman92 plugin loader swaps some of them for bigger ones at run
  time, and the exported symbol is then an empty array nobody uses.
* Before hooking anything the first bytes of each function are compared with the ones of the analysed build; a build of
  the game that does not match just gets the corresponding feature disabled (and a line in the log).

## Verification

Done:

* Unit tests (222 checks) of paths, `modloader.ini`, mod scanning and plugin dispatch, VFS priorities, the `gta.dat`
  merge, the IMG writer, the streaming patcher against a fake game, and the hooks themselves driven through a fake host
  (redirect, fallback to the original name, `gta.dat` generation through `OS_FileRead`, fingerprint refusal). They run
  natively (also under AddressSanitizer/UBSan) and as ARM32 under qemu.
* Compiled with GCC 13 and clang 18 (`-Wall -Wextra`, no warnings) for ARM32, including the AML entry point against
  the real AML headers.
* `tools/verify_libgtasa.py libGTASA.so` checks a library against what the port needs. It was run against the
  `libGTASA.so` this port was written for (sha256 `46c1a97f5423520e4965850cc034648e8a2a11c95f55b24788a4fa208506b9db`, a
  2.00 build that also carries Fastman92 sections): every symbol is there, every fingerprint and object size matches.
* The behaviour of the hooked functions (what `OS_FileOpen`/`NvFOpen` do with absolute and relative names, return values
  of `OS_FileRead`, the "first entry wins" rule and the layouts of `CStreamingInfo`/`ms_files` in `LoadCdDirectory`,
  `AddImageToList`, `GetCdPosnAndSize`...) comes from reading the disassembly of that library.

* Built by GitHub Actions with the Android NDK r30 (clang 21): compiles and links; the library exports only the AML entry
  points (`exports.map`).

Not done: running it on a device.

## Notes for upstream

* Purely additive: a new `android/` directory and `.github/workflows/android.yml`. **No existing file is touched**,
  the Windows build and its CI are unaffected.
* Reused from the original tree as they are: `include/modloader/modloader.h` (the plugin ABI),
  `include/modloader/util/hash.hpp`, `src/shared/ini_parser/ini_parser.hpp`.
  `include/modloader/util/path.hpp`/`utility.hpp` (Win32 only) and the core (`src/core`) were *not* reused:
  the Android core is smaller and POSIX only, it implements the same behaviour (profiles, priorities, plugin dispatch).
  A later step could move both onto one platform layer.
* Everything except `src/aml_entry.cpp` is independent from AML and from the game, which is what makes it testable on a PC.
