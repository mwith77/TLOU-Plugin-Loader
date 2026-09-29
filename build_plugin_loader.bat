@echo off
rem Build version.dll and tlou_plugin_loader.dll.
rem Run from "x64 Native Tools Command Prompt for VS".
rem
rem version.dll is a thin shim placed in the game folder. The game imports
rem version.dll, so Windows loads this one in the real one's place; it forwards
rem every real version.dll export to the system copy and loads
rem tlou_plugin_loader.dll, the real loader, which loads a DLL from each mod's
rem folder under the game's Mods folder. No file of the game's own is changed.

where cl.exe   >nul 2>nul || (echo cl.exe not found - use the x64 Native Tools prompt & exit /b 1)
where ml64.exe >nul 2>nul || (echo ml64.exe not found & exit /b 1)
where rc.exe   >nul 2>nul || (echo rc.exe not found & exit /b 1)

rc /nologo /fo version_shim.res version_shim.rc
if errorlevel 1 (echo RC version_shim FAILED & exit /b 1)
rc /nologo /fo tlou_plugin_loader.res tlou_plugin_loader.rc
if errorlevel 1 (echo RC tlou_plugin_loader FAILED & exit /b 1)

ml64 /nologo /c /Fo hook_stub_loader.obj hook_stub.asm
if errorlevel 1 (echo ASSEMBLE hook_stub FAILED & exit /b 1)
ml64 /nologo /c /Fo version_shim_asm.obj version_shim.asm
if errorlevel 1 (echo ASSEMBLE version_shim FAILED & exit /b 1)

rem The real loader. version.lib is for the game version it reads.
cl /nologo /LD /O2 /EHsc /W4 /w14459 tlou_plugin_loader.cpp hook_finder.cpp ^
   hook_stub_loader.obj tlou_plugin_loader.res ^
   /link version.lib /OUT:tlou_plugin_loader.dll
if errorlevel 1 (echo LOADER BUILD FAILED & exit /b 1)

rem The shim. /Brepro builds it the same bytes every time.
cl /nologo /LD /O2 /EHsc /W4 /Brepro version_shim.cpp version_shim_asm.obj ^
   version_shim.res /link /Brepro /OUT:version.dll
if errorlevel 1 (echo SHIM BUILD FAILED & exit /b 1)

del /q tlou_plugin_loader.obj hook_finder.obj hook_stub_loader.obj ^
       version_shim.obj version_shim_asm.obj tlou_plugin_loader.exp ^
       tlou_plugin_loader.lib version.exp version.lib ^
       version_shim.res tlou_plugin_loader.res 2>nul

echo.
echo Built version.dll and tlou_plugin_loader.dll
