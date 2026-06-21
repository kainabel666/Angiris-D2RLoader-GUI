@echo off
setlocal enabledelayedexpansion
title D2R Launcher - MinGW Build
color 0A
echo ================================================
echo  D2R Mod Launcher - MinGW-w64 Compiler
echo ================================================
echo.

REM ── Locate g++ and its bin directory ─────────────────────────────────────
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

REM Not on PATH — scan every drive letter, J first since that's where MSYS2 is
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
REM ── THE KEY FIX ──────────────────────────────────────────────────────────
REM g++ needs libgcc_s_seh-1.dll, libisl-23.dll, libgmp-10.dll, libmpc-3.dll
REM ALL of which live in the same bin\ folder as g++.exe itself.
REM Putting that folder at the FRONT of PATH lets Windows find them.
set "PATH=!GXXDIR!;%PATH%"

echo [OK] Compiler : !GXX!
echo [OK] Bin dir  : !GXXDIR!
echo [OK] DLL fix  : !GXXDIR! added to front of PATH
echo.

REM Quick sanity-check — if g++ still can't run, bail with a clear message
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

REM ── Compile ──────────────────────────────────────────────────────────────
set "SRCDIR=%~dp0"
REM ── Source file selection ────────────────────────────────────────────
REM The redesign lives in Angiris.cpp; the legacy build remains at
REM D2R_ModLauncher.cpp as a fallback while phases land. If Angiris.cpp
REM is present we build it; otherwise we fall back to the legacy file.
if exist "%SRCDIR%Angiris.cpp" (
    set "SRC=%SRCDIR%Angiris.cpp"
    set "OUT=%SRCDIR%Angiris.exe"
    echo [INFO] Building redesign: Angiris.cpp
) else (
    set "SRC=%SRCDIR%D2R_ModLauncher.cpp"
    set "OUT=%SRCDIR%D2R_ModLauncher.exe"
    echo [INFO] Building legacy: D2R_ModLauncher.cpp
)
set "LOG=%SRCDIR%compile_errors.txt"

echo Source  : %SRC%
echo Output  : %OUT%
echo.
echo Compiling... (20-40 seconds)
echo.

REM ── Step 1: compile the resource script (icon + version info) ──────
REM windres ships alongside g++ in MSYS2 UCRT64. It produces a COFF
REM object file the linker can pull in alongside the .cpp output.
set "RC=%SRCDIR%angiris.rc"
set "RES_OBJ=%SRCDIR%angiris.res.o"

if exist "%RC%" (
    echo [1/2] Compiling resources: angiris.rc
    "!GXXDIR!windres.exe" -i "%RC%" -O coff -o "%RES_OBJ%" 2>> "%LOG%"
    if !ERRORLEVEL! neq 0 (
        echo [WARN] Resource compile failed — building without icon/version info.
        echo        See %LOG% for details.
        set "RES_OBJ="
    )
) else (
    echo [INFO] No angiris.rc found — exe will use default icon.
    set "RES_OBJ="
)

REM ── Step 2: compile + link the C++, including the resource object ──
echo [2/2] Compiling C++ and linking
REM -static links libgcc, libstdc++, AND libwinpthread statically in one shot.
REM Result: a single self-contained exe, no runtime DLLs needed.
"!GXX!" -O2 -std=c++17 -mwindows -municode ^
    "%SRC%" "!RES_OBJ!" -o "%OUT%" ^
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
