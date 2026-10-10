@echo off
setlocal
rem Build tlou_plugin_loader.asi. Run from the "x64 Native Tools Command Prompt for VS".

rem Work in this script's own folder, wherever the prompt is.
cd /d "%~dp0"

where cl.exe   >nul 2>nul || (echo cl.exe not found - use the x64 Native Tools prompt & exit /b 1)
where ml64.exe >nul 2>nul || (echo ml64.exe not found & exit /b 1)
where rc.exe   >nul 2>nul || (echo rc.exe not found & exit /b 1)

rem The version resource. It also carries tlou_plugin_loader.ini and
rem loadorder.txt, which the loader writes into the game where either is
rem missing or empty.
rc /nologo /fo tlou_plugin_loader.res tlou_plugin_loader.rc
if errorlevel 1 (echo RC FAILED & exit /b 1)

rem The hook stubs.
ml64 /nologo /c /Fo hook_stub_loader.obj hook_stub.asm
if errorlevel 1 (echo ASSEMBLE hook_stub FAILED & exit /b 1)

rem The loader. version.lib is for the game version it reads.
rem tlou_plugin_loader.map turns an offset in a crash line back into a
rem function name.
cl /nologo /LD /O2 /EHsc /W4 /w14459 tlou_plugin_loader.cpp hook_finder.cpp ^
   hook_stub_loader.obj tlou_plugin_loader.res ^
   /link version.lib /OUT:tlou_plugin_loader.asi /MAP:tlou_plugin_loader.map
if errorlevel 1 (echo BUILD FAILED & exit /b 1)

del /q tlou_plugin_loader.obj hook_finder.obj hook_stub_loader.obj ^
       tlou_plugin_loader.exp tlou_plugin_loader.lib ^
       tlou_plugin_loader.res 2>nul

echo.
echo Built tlou_plugin_loader.asi
exit /b 0
