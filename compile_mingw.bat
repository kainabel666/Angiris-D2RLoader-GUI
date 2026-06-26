@echo off
setlocal enabledelayedexpansion
title D2R Launcher - MinGW Build
color 0A
echo ================================================
echo  D2R Mod Launcher - MinGW-w64 Compiler
echo ================================================
echo.

REM -- Locate g++ and its bin directory --------------------------------------
set GXX=
set GXXDIR=

where g++ >nul 2>&1
if %ERRORLEVEL%==0 (
    for /f "delims=" %%P in ('where g++') do (
        if "!GXX!"=="" (
            set "GXX=%%P"
            set "GXXDIR=%%~dpP"
        )
    )
)

if not "!GXX!"=="" goto :fixpath

REM Not on PATH - scan every drive letter, J first since that's where MSYS2 is
echo g++ not found on PATH. Scanning drives (J first)...
for %%D in (J C D E F G H I K L M N O P Q R S T U V W X Y Z A B) do (
    for %%F in (msys64 msys2) do (
        for %%S in (ucrt64 mingw64 mingw32) do (
            if exist "%%D:\%%F\%%S\bin\g++.exe" (
                set "GXX=%%D:\%%F\%%S\bin\g++.exe"
                set "GXXDIR=%%D:\%%F\%%S\bin\"
                goto :fixpath
            )
        )
    )
)

echo.
echo [ERROR] g++.exe not found on any drive.
echo.
echo Open MSYS2 UCRT64 and run:  pacman -S mingw-w64-ucrt-x86_64-gcc
echo.
pause
exit /b 1

:fixpath
REM g++ needs libgcc_s_seh-1.dll, libisl-23.dll, libgmp-10.dll, libmpc-3.dll
REM all of which live in the same bin\ folder as g++.exe itself.
REM Putting that folder at the FRONT of PATH lets Windows find them.
set "PATH=!GXXDIR!;%PATH%"

echo [OK] Compiler : !GXX!
echo [OK] Bin dir  : !GXXDIR!
echo [OK] DLL fix  : !GXXDIR! added to front of PATH
echo.

REM Quick sanity-check - if g++ still can't run, bail with a clear message
"%GXX%" --version >nul 2>&1
if %ERRORLEVEL% neq 0 (
    echo [ERROR] g++ crashes even after PATH fix.
    echo         MSYS2 install may be incomplete or corrupted.
    echo         Try:  pacman -S mingw-w64-ucrt-x86_64-gcc  in MSYS2 UCRT64
    pause
    exit /b 1
)
"%GXX%" --version
echo.

REM -- Source files ----------------------------------------------------------
REM Phase 1:  4 leaf modules (core, version, ini_editor, http)
REM Phase 2:  4 persistence modules (config, update_cache, playtime, seeds)
REM Phase 3a: 4 mod-system modules (mod_scan, launch_flags, mod_config,
REM           tool_resolver) plus mod_types.h (header-only)
REM Phase 3b: 5 mod-operation modules (fs_utils, mod_updates, save_backup,
REM           zip_install, launcher_self_update)
REM Phase 4a: 2 paint-resource modules (assets, fonts)
REM Phase 4b: 1 customization module (layout)
REM Phase 4c: 2 design-token modules (scaling, colors)
REM Phase 5:  2 UI control modules (hover_tip, mod_list)
REM           plus LO namespace promoted to layout.h
REM Feature: plugin_manager (Loader Options Plugins button)
REM Phase 6:  1 dialogs module (6 themed modal dialogs)
REM           plus ButtonKind+MkStdBtn promoted to buttons.h
REM           plus control_ids.h (already in 5.5)
REM Phase 7a: button machinery (RegisterButton, MkStdBtn, hover subclass,
REM           WM_DRAWITEM body) extracted to buttons.cpp
REM Phase 7b: paint primitives (FillSolid, DrawGoldText, DrawFlagCheckbox,
REM           OPDrawBtnFrame, PaintTopOrnament, PaintCornerAccents)
REM           extracted to paint_helpers.cpp
REM Phase 7c: big paint functions (PaintBody, PaintLeftRail, PaintModDescription,
REM           PaintLaunchOptions, PaintBottomPanel, PaintToolbarControl)
REM           extracted to paint_main.cpp
REM           plus BodyLayout/LeftPanelGeom/Body*Rect promoted to layout.h,
REM           UI globals exposed via new ui_state.h, LoaderOpts to ui_state.h
REM Phase 7d: Layout, RefreshMods, RefreshModDescriptionLinks, RepositionForExpansion,
REM           ComputeBodyLayout, ComputeLeftPanelGeom, Body*Rect bodies all moved
REM           from Angiris.cpp into layout.cpp; HWND control globals exposed via
REM           ui_state.h
REM Add new .cpp files here as the modularization progresses.
set "SRCDIR=%~dp0"
REM (former SOURCES variable removed - was unused and contained a
REM `set "VAR=^...` quoted multiline that cmd mis-parsed, causing
REM Windows to "execute" the first .cpp path on each run, which
REM opened Angiris.cpp in VS Code before the build could start.
REM The g++ invocation below lists each source file directly.)

REM Sanity check: every required source file must exist
for %%S in (Angiris.cpp core.cpp version.cpp ini_editor.cpp http.cpp config.cpp update_cache.cpp playtime.cpp seeds.cpp mod_scan.cpp launch_flags.cpp mod_config.cpp tool_resolver.cpp fs_utils.cpp mod_updates.cpp save_backup.cpp zip_install.cpp launcher_self_update.cpp assets.cpp fonts.cpp layout.cpp scaling.cpp colors.cpp hover_tip.cpp mod_list.cpp plugin_manager.cpp dialogs.cpp buttons.cpp paint_helpers.cpp paint_main.cpp) do (
    if not exist "%SRCDIR%%%S" (
        echo [ERROR] Missing source file: %SRCDIR%%%S
        pause
        exit /b 1
    )
)

set "OUT=%SRCDIR%Angiris.exe"
set "LOG=%SRCDIR%compile_errors.txt"

echo Sources :
echo   %SRCDIR%Angiris.cpp
echo   %SRCDIR%core.cpp
echo   %SRCDIR%version.cpp
echo   %SRCDIR%ini_editor.cpp
echo   %SRCDIR%http.cpp
echo   %SRCDIR%config.cpp
echo   %SRCDIR%update_cache.cpp
echo   %SRCDIR%playtime.cpp
echo   %SRCDIR%seeds.cpp
echo   %SRCDIR%mod_scan.cpp
echo   %SRCDIR%launch_flags.cpp
echo   %SRCDIR%mod_config.cpp
echo   %SRCDIR%tool_resolver.cpp
echo   %SRCDIR%fs_utils.cpp
echo   %SRCDIR%mod_updates.cpp
echo   %SRCDIR%save_backup.cpp
echo   %SRCDIR%zip_install.cpp
echo   %SRCDIR%launcher_self_update.cpp
echo   %SRCDIR%assets.cpp
echo   %SRCDIR%fonts.cpp
echo   %SRCDIR%layout.cpp
echo   %SRCDIR%scaling.cpp
echo   %SRCDIR%colors.cpp
echo   %SRCDIR%hover_tip.cpp
echo   %SRCDIR%mod_list.cpp
echo   %SRCDIR%plugin_manager.cpp
echo   %SRCDIR%dialogs.cpp
echo   %SRCDIR%buttons.cpp
echo   %SRCDIR%paint_helpers.cpp
echo   %SRCDIR%paint_main.cpp
echo Output  : %OUT%
echo.
echo Compiling... (20-40 seconds)
echo.

REM -- Step 1: compile the resource script (icon + version info) ------------
REM windres ships alongside g++ in MSYS2 UCRT64. It produces a COFF object
REM file the linker can pull in alongside the .cpp output.
set "RC=%SRCDIR%angiris.rc"
set "RES_OBJ=%SRCDIR%angiris.res.o"

if exist "%RC%" (
    echo [1/2] Compiling resources: angiris.rc
    "!GXXDIR!windres.exe" -i "%RC%" -O coff -o "%RES_OBJ%" 2>> "%LOG%"
    if !ERRORLEVEL! neq 0 (
        echo [WARN] Resource compile failed - building without icon/version info.
        echo        See %LOG% for details.
        set "RES_OBJ="
    )
) else (
    echo [INFO] No angiris.rc found - exe will use default icon.
    set "RES_OBJ="
)

REM -- Step 2: compile + link the C++, including the resource object --------
echo [2/2] Compiling C++ and linking
REM -static links libgcc, libstdc++, AND libwinpthread statically in one shot.
REM Result: a single self-contained exe, no runtime DLLs needed.
"!GXX!" -O2 -std=c++17 -mwindows -municode ^
    "%SRCDIR%Angiris.cpp" ^
    "%SRCDIR%core.cpp" ^
    "%SRCDIR%version.cpp" ^
    "%SRCDIR%ini_editor.cpp" ^
    "%SRCDIR%http.cpp" ^
    "%SRCDIR%config.cpp" ^
    "%SRCDIR%update_cache.cpp" ^
    "%SRCDIR%playtime.cpp" ^
    "%SRCDIR%seeds.cpp" ^
    "%SRCDIR%mod_scan.cpp" ^
    "%SRCDIR%launch_flags.cpp" ^
    "%SRCDIR%mod_config.cpp" ^
    "%SRCDIR%tool_resolver.cpp" ^
    "%SRCDIR%fs_utils.cpp" ^
    "%SRCDIR%mod_updates.cpp" ^
    "%SRCDIR%save_backup.cpp" ^
    "%SRCDIR%zip_install.cpp" ^
    "%SRCDIR%launcher_self_update.cpp" ^
    "%SRCDIR%assets.cpp" ^
    "%SRCDIR%fonts.cpp" ^
    "%SRCDIR%layout.cpp" ^
    "%SRCDIR%scaling.cpp" ^
    "%SRCDIR%colors.cpp" ^
    "%SRCDIR%hover_tip.cpp" ^
    "%SRCDIR%mod_list.cpp" ^
    "%SRCDIR%plugin_manager.cpp" ^
    "%SRCDIR%dialogs.cpp" ^
    "%SRCDIR%buttons.cpp" ^
    "%SRCDIR%paint_helpers.cpp" ^
    "%SRCDIR%paint_main.cpp" ^
    "!RES_OBJ!" -o "%OUT%" ^
    -static ^
    -lgdiplus -lcomctl32 -lshell32 -ladvapi32 -lcomdlg32 -lshlwapi -lole32 -luuid -lwinhttp ^
    >> "%LOG%" 2>&1

if %ERRORLEVEL% neq 0 (
    echo ================================================
    echo  [FAILED]  Error code: %ERRORLEVEL%
    echo ================================================
    echo.
    type "%LOG%"
    echo.
    echo Full log saved: %LOG%
    pause
    exit /b 1
)

if not exist "%OUT%" (
    echo [ERROR] Compile reported success but no exe was created.
    echo Check: %LOG%
    pause
    exit /b 1
)

del "%LOG%" >nul 2>&1
if exist "%RES_OBJ%" del "%RES_OBJ%" >nul 2>&1

echo ================================================
echo  SUCCESS!  Build complete.
echo ================================================
echo.
echo Fully self-contained single-file exe - no DLLs required.
echo Location: %OUT%
echo.
choice /C YN /M "Launch it now?"
if %ERRORLEVEL%==1 start "" "%OUT%"
pause
endlocal
