# TLOU Plugin Loader

Loads DLL plugins into The Last of Us Part I and The Last of Us Part II on PC.
The loader is `tlou_plugin_loader.asi`, an ordinary DLL with the `.asi`
extension, which the [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader)
(`winmm.dll` beside the game executable) loads when the game starts. It loads a
DLL from each mod's folder under the game's `Mods` folder. The game is started
normally, from Steam. No file of the game's own is changed.

Version 1.1.0, for `tlou-i.exe` 1.1.5.0 and `tlou-ii.exe` 1.6.0. It needs a
processor with AVX, as both games do.

## Files

| File | Contents |
| --- | --- |
| `tlou_plugin_loader.cpp`, `tlou_plugin_loader.rc` | The loader: loads the plugins, owns every hook and writes crash reports. |
| `hook_finder.cpp`, `hook_finder.h` | Finds hook sites in the game's code, by byte pattern or by instruction shape, and caches them in `Mods\Plugin Loader\hook_cache.ini`. |
| `hook_stub.asm` | The 256 stubs a hook jumps to; each keeps the game's registers, vector registers and flags. |
| `tlou_plugin_sdk.h` | The header a plugin includes. |
| `build_plugin_loader.bat` | Builds `tlou_plugin_loader.asi`. |
| `tlou_plugin_loader.ini` | The loader's default settings, built into the `.asi`. |
| `loadorder.txt` | The default load order, built into the `.asi`. |

## Building

From the x64 Native Tools Command Prompt for Visual Studio, which provides `cl`,
`ml64` and `rc`, run `build_plugin_loader.bat` in this folder. It builds
`tlou_plugin_loader.asi`.

## Installing

Laid out as it goes into the game folder:

    winmm.dll                       the Ultimate ASI Loader, beside the game executable
    tlou_plugin_loader.asi          beside the game executable
    Mods\Plugin Loader\             the loader's own folder

At its first start the loader writes `tlou_plugin_loader.ini` and
`loadorder.txt` into `Mods\Plugin Loader` where they are missing or empty, and
adds any setting a file lacks, so updating never resets them. Each mod goes in
a folder of its own under `Mods`; the loader loads the first DLL directly
inside each folder under `Mods` other than its own.

Updating from 1.0: delete `version.dll` beside the game executable and
`Mods\Plugin Loader\tlou_plugin_loader.dll`; a `Mods\loadorder.txt` is moved
into `Mods\Plugin Loader` at the first start. A second copy of the loader in
the same game does nothing and says so in the log.

Uninstalling is deleting `tlou_plugin_loader.asi` and the `Mods` folder (and
`winmm.dll` where no other mod uses it). The game then runs vanilla, and no
save depends on anything the loader added.

## Load order

`loadorder.txt` names plugins by mod folder or by DLL file name, one per line.
The plugins it names load first, in its order; the rest load after them, sorted
by DLL file name. Lines beginning with `;` or `#` are ignored.

## Writing a plugin

A plugin is a DLL that exports:

    int  Init(LoaderHandle loader);          required
    int  SupportsGame(LoaderHandle loader);  optional, absent means yes

Define `TLOU_PLUGIN` before including `tlou_plugin_sdk.h`, and call `loaderBind`
from `Init` or `SupportsGame` before using any loader function the header wraps.
A mod keeps its ini, its log and any cache in its own folder, which
`loaderModFolder` names.

## Logs and crash reports

The loader keeps one log, `Mods\Plugin Loader\tlou_plugin_loader.log`, newest
line first, at most 1 MB, and writes only failures, errors and crash reports. A
crash report names the fault, gives its address as a module and an offset, the
address touched, the registers, and the return addresses on the stack, each
with its module.
