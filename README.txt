================================================================================
                      Angiris Launcher  -  BUILD INSTRUCTIONS
================================================================================

The launcher is a single .cpp file plus an optional resource script.
Two supported build paths:

  A) MSYS2 / MinGW-w64  (UCRT64)   - the project's primary build
  B) Visual Studio 2019 or 2022    - works equally well, no source changes

Both produce a single self-contained Angiris.exe with no DLL dependencies.


--------------------------------------------------------------------------------
  FILES YOU NEED
--------------------------------------------------------------------------------

In one folder:

    Angiris.cpp              the entire launcher source
    angiris.rc               resource script (icon + version info) - optional
    angiris.ico              the launcher icon - optional, referenced by .rc

The .rc and .ico are optional: leaving them out just gives you an .exe with
the default Windows icon. Functionality is identical.


================================================================================
  OPTION A:  MSYS2 / MinGW-w64
================================================================================

This is the build the project uses day-to-day. If you've ever built a
Windows C++ program with MSYS2 before, you have everything you need.


PREREQUISITES
--------------------------------------------------------------------------------

1. Install MSYS2 from https://www.msys2.org/ (run the installer, accept the
   defaults).

2. Open the "MSYS2 UCRT64" shortcut from your Start menu (NOT "MSYS2 MSYS" -
   the UCRT64 environment is what the launcher targets).

3. Install the toolchain:

       pacman -S mingw-w64-ucrt-x86_64-gcc

   Accept the prompts. Takes a couple minutes.

4. Confirm it works - from MSYS2 UCRT64:

       g++ --version

   You should see "g++.exe (Rev?, Built by MSYS2 project)" followed by a
   version number (15.x is typical for current MSYS2).


BUILD
--------------------------------------------------------------------------------

Drop compile_mingw.bat in the same folder as Angiris.cpp and double-click
it. The script:

  1. Locates g++ - either on PATH, or by scanning drives for an MSYS2 install
  2. Compiles angiris.rc via windres (skipped if no .rc is present)
  3. Compiles + links Angiris.cpp into a single static .exe

About 20-40 seconds. On success it prompts to launch the result.


WHAT THE SCRIPT DOES (so you can replicate manually)
--------------------------------------------------------------------------------

If you'd rather invoke g++ yourself, the commands are:

    windres -i angiris.rc -O coff -o angiris.res.o

    g++ -O2 -std=c++17 -mwindows -municode ^
        Angiris.cpp angiris.res.o -o Angiris.exe ^
        -static ^
        -lgdiplus -lcomctl32 -lshell32 -ladvapi32 ^
        -lcomdlg32 -lshlwapi -lole32 -luuid -lwinhttp

Key flags:
    -mwindows       Windows GUI subsystem (no console window)
    -municode       UNICODE / _UNICODE both defined; wmain entry point
    -static         link libgcc, libstdc++, and libwinpthread statically -
                    no MinGW DLLs required at runtime
    -std=c++17      the launcher uses C++17 features

Linked libraries:
    gdiplus     - the entire painted UI (frames, fonts, images, anti-alias)
    comctl32    - standard Windows control style hooks
    shell32     - Drag-and-drop file targeting, ShellExecute, etc.
    advapi32    - registry access (D2R install path autodetect)
    comdlg32    - folder-picker dialog
    shlwapi     - path manipulation helpers
    ole32, uuid - OLE plumbing pulled in by some shell APIs
    winhttp     - update checks + launcher self-update download


================================================================================
  OPTION B:  VISUAL STUDIO 2019 / 2022
================================================================================

VS Community is free for personal and open-source use. Slightly more clicks
to set up than the MinGW path, but no source changes needed.


PREREQUISITES
--------------------------------------------------------------------------------

1. Install Visual Studio Community from
   https://visualstudio.microsoft.com/vs/community/

2. In the installer, check the "Desktop development with C++" workload.
   This includes the MSVC compiler, the Windows SDK, and the rest of what
   you'll need. Other workloads can be left off.


CREATE THE PROJECT
--------------------------------------------------------------------------------

1. File -> New -> Project
2. Pick "Empty Project" (C++ template). Name it whatever you want -
   "AngirisLauncher" works. Create.
3. In Solution Explorer (the tree view on the right):
       Right-click "Source Files" -> Add -> Existing Item ->  select
            Angiris.cpp
       Right-click "Resource Files" -> Add -> Existing Item -> select
            angiris.rc  AND  angiris.ico
4. Open project properties: right-click the project name -> Properties
   (or press Alt+Enter on the project).


CONFIGURE THE PROJECT
--------------------------------------------------------------------------------

At the top of the Properties dialog:
    Configuration:  All Configurations    (so the settings apply to Debug
                                          and Release)
    Platform:       All Platforms          (or pick x64 if you only want
                                          a 64-bit build)

Then set each of these. The path on the left is the navigation tree in
the Properties dialog.

    Configuration Properties -> General
        C++ Language Standard:        ISO C++17 Standard (/std:c++17)
        Character Set:                Use Unicode Character Set

    Configuration Properties -> C/C++ -> Code Generation
        Runtime Library:              Multi-threaded (/MT)
                                      (Release configuration)
                                      Multi-threaded Debug (/MTd) for Debug.
        ^^ This is the equivalent of MinGW's -static flag. Statically
           links the C++ runtime so end users don't need vcredist.

    Configuration Properties -> Linker -> System
        SubSystem:                    Windows (/SUBSYSTEM:WINDOWS)

    Configuration Properties -> Linker -> Input
        Additional Dependencies:      add these to the front of the list
                                      (semicolon-separated):

            gdiplus.lib;comctl32.lib;shell32.lib;advapi32.lib;
            comdlg32.lib;shlwapi.lib;ole32.lib;uuid.lib;winhttp.lib


BUILD
--------------------------------------------------------------------------------

1. Set the configuration dropdown at the top of VS to "Release" and
   "x64" (or "x86" for a 32-bit build).
2. Build -> Build Solution  (or press F7, or Ctrl+Shift+B).
3. Output ends up at:
       <project folder>\x64\Release\AngirisLauncher.exe
   (or x86\Release\ depending on platform)

The .rc file is compiled automatically - VS handles rc.exe under the hood
when the resource is in the project.


================================================================================
  COMMON GOTCHAS
================================================================================

* "Missing libgcc_s_seh-1.dll" when launching the MinGW build.
      The build wasn't fully static. Make sure -static is on the g++ command
      line; the supplied compile_mingw.bat already includes it.

* "vcredist missing" / "MSVCP140.dll not found" on the Visual Studio build.
      The Runtime Library setting was left at /MD (DLL CRT) instead of /MT.
      Change to /MT and rebuild.

* "Cannot find gdiplus.h" or similar header errors.
      The Windows SDK isn't installed (Visual Studio) or the UCRT64
      environment isn't being used (MinGW). Install / re-open the correct
      environment.

* The .exe runs but has no icon.
      The .rc didn't get compiled in. For MinGW, make sure windres ran
      successfully (check compile_errors.txt if the script left one). For
      VS, make sure angiris.rc was added to the project under Resource
      Files.

* Antivirus flags the .exe.
      Unsigned single-file binaries from unknown sources sometimes trigger
      heuristics. Sign the binary with a code-signing certificate, or
      submit it to the antivirus vendor's false-positive form, or just
      tell your users to add an exclusion. There's no fix from the build
      side.


================================================================================
  NOTES ON MAINTAINING SOURCE COMPATIBILITY
================================================================================

The launcher source is portable between MinGW UCRT64 and MSVC with no
preprocessor branches. A few things to keep in mind if you modify it:

* Wide-string printf format specifiers always use %ls, never %s.
  MinGW's UCRT64 default interprets %s in wide-format calls as a NARROW
  string (truncates at the first null byte). %ls is unambiguous and works
  on both compilers.

* The codebase is plain Win32 + GDI+. No third-party libraries pulled in
  via vcpkg, NuGet, or pacman beyond what ships with the compiler.

* C++17 is the floor. Some pieces (std::filesystem usage in helper code,
  if-with-init, structured bindings) won't compile under older standards.


================================================================================
