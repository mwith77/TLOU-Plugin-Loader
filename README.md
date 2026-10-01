# TLOU Plugin Loader

Loads DLL plugins into The Last of Us Part I on PC. It is two DLLs. `version.dll`
sits in the game folder; the game imports `version.dll`, so Windows loads this
one in place of the system copy. It forwards every real `version.dll` export to
the system copy and loads `tlou_plugin_loader.dll`, which loads a DLL from each
mod's folder under the game's `Mods` folder. The game is started normally, from
Steam. No file of the game's own is changed.

Version 1.0.1, for `tlou-i.exe` 1.1.5.0.

## Files

| File | Contents |
| --- | --- |
| `version_shim.cpp`, `version_shim.asm`, `version_shim.rc` | The shim built as `version.dll`: forwards the real `version.dll` exports and loads the real loader. |
| `tlou_plugin_loader.cpp`, `tlou_plugin_loader.rc` | The loader: loads the plugins, owns every hook and writes crash reports. |
| `hook_finder.cpp`, `hook_finder.h` | Finds hook sites in the game's code, by byte pattern or by instruction shape, and caches them in `Mods\Plugin Loader\Cache\hook_cache.ini`. |
| `hook_stub.asm` | The 256 stubs a hook jumps to; each saves the registers the callbacks read. |
| `tlou_plugin_sdk.h` | The header a plugin includes. |
| `build_plugin_loader.bat` | Builds both DLLs. |
| `tlou_plugin_loader.ini` | Settings for the loader, and for forcing a plugin to load. |
| `loadorder.txt` | The order plugins load in. |

## Building

From the x64 Native Tools Command Prompt for Visual Studio, which provides `cl`,
`ml64` and `rc`, run `build_plugin_loader.bat` in this folder. It builds
`version.dll` and `tlou_plugin_loader.dll`.

## Installing

Laid out as it goes into the game folder:

    version.dll                     beside the game executable
    Mods\loadorder.txt              the order plugins load in
    Mods\Plugin Loader\             tlou_plugin_loader.dll and tlou_plugin_loader.ini

Start the game normally, from Steam. `version.dll` never has to change once it is
in place; later loader releases replace only `tlou_plugin_loader.dll`.

Windows also loads `version.dll` into the other programs in the game folder that
import it, such as the game's crash reporter, `crs-handler.exe`. In them it only
forwards the real `version.dll`; the loader does nothing outside the game's own
executables, `tlou-i.exe` and `tlou-i-l.exe`. Each mod
goes in a folder of its own under `Mods`; the loader loads the first DLL directly
inside each folder under `Mods` other than its own.

Uninstalling is deleting `version.dll` and the `Mods` folder. The game then runs
vanilla, and no save depends on anything the loader added.

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
A mod's ini sits beside its DLL, and its log and cache go in its folder's `Logs`
and `Cache`.

## Logs and crash reports

The loader keeps its log in `Mods\Plugin Loader\Logs` and writes only failures,
errors and crash reports. Each log holds one run: at start, the previous run's
log is moved to `Logs\Archive`. A crash report names the fault, gives its address
as a module and an offset, the address touched, the registers, and the return
addresses on the stack, each with its module.
