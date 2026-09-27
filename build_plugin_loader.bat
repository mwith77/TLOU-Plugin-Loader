@echo off
rem Build tlou_plugin_loader.dll and tlou_plugin_launcher.exe.
rem Run from "x64 Native Tools Command Prompt for VS".

where cl.exe   >nul 2>nul || (echo cl.exe not found - use the x64 Native Tools prompt & exit /b 1)
where ml64.exe >nul 2>nul || (echo ml64.exe not found & exit /b 1)

ml64 /nologo /c /Fo hook_stub_loader.obj hook_stub.asm
if errorlevel 1 (echo ASSEMBLE FAILED & exit /b 1)

cl /nologo /LD /O2 /EHsc /W4 /w14459 tlou_plugin_loader.cpp hook_finder.cpp ^
   hook_stub_loader.obj /link version.lib /OUT:tlou_plugin_loader.dll
if errorlevel 1 (echo LOADER BUILD FAILED & exit /b 1)

rem A windowed program, so no console appears when it runs. mainCRTStartup
rem keeps main() as the entry point.
cl /nologo /O2 /EHsc /W4 /w14459 tlou_plugin_launcher.cpp ^
   /link user32.lib /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup ^
   /OUT:tlou_plugin_launcher.exe
if errorlevel 1 (echo LAUNCHER BUILD FAILED & exit /b 1)

del /q tlou_plugin_loader.obj tlou_plugin_launcher.obj hook_finder.obj hook_stub_loader.obj tlou_plugin_loader.exp tlou_plugin_loader.lib 2>nul

echo.
echo Built tlou_plugin_loader.dll and tlou_plugin_launcher.exe
