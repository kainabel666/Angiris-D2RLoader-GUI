================================================================================
                      Angiris Launcher  -  D2RLoader MOD LAUNCHER
                              by KainAbel666
================================================================================

A standalone launcher for Diablo II: Resurrected mods. Picks up your installed
mods automatically, remembers your launch flags AND seed per-mod, watches your
mods folder for changes, checks for mod updates, lets you re-skin the UI on
the fly (scale / font / accent color), tracks your per-mod playtime, and gives
modders quick access to their development tools.

The launcher drives D2RLoader.exe (the loader/injector). Make sure D2RLoader is
installed in your Diablo II: Resurrected folder.


--------------------------------------------------------------------------------
  WHAT IT DOES
--------------------------------------------------------------------------------

* Auto-detects your D2R install location (registry + common paths)
* Mod list with per-mod banner images (or a clean text fallback)
* Mod Description panel with optional Discord / Docs / Website buttons
  (driven by each mod's modinfo.json)
* Per-mod update checking with a "[Details]" link to the mod's source page
* Per-mod playtime tracking - cumulative play time + last-played date, shown
  as a hover tooltip on each mod row
* Right-click context menu on each mod row: backup saves, re-zip the mod
  folder, or uninstall the mod
* Automatic save-folder backups before destructive actions (Overwrite install
  and Uninstall), with rotation (most recent 5 kept)
* Drag-and-drop mod installer - drop .zip files onto the launcher to install
* Self-updating - the launcher checks for its own new versions on GitHub,
  flags the version label gold when one's available, and can install the
  update in place with a Restart button
* Seven command-line args, all saved per-mod:
      Use Txts   (-txt)             (locked on - required by D2RLoader)
      No Sound   (-ns)
      Window     (-w)
      Skip Intro (-skiplogovideo)
      Respec     (-enablerespec)
      Reset Maps (-resetofflinemaps)
      Seed       (-seed VALUE)      (checkbox + typeable input + dropdown)
* Live launch-command preview that auto-grows from 1 to 3 wrapped lines as
  the args expand, and shrinks back when you remove flags
* Appearance toolbar under the mod list:
      Scale  - 3-state slider for UI size (DPI-aware preset set)
      Font   - dropdown of bundled .ttf fonts (or whatever's in assets\fonts\)
      Colour - dropdown of 8 accent colors (gold/red/green tones)
* Loader Options (written straight to D2RLoader.toml):
      Basic Options       - modal exposing 6 [d2rcore.*] and
                            [d2rloader] settings including Show Sockets,
                            Show Item Level, Respec Skill/Stats,
                            Stash Tabs (0-16), Material Limit (0-255),
                            and Show TCP/IP Button
      Developer Options   - modal exposing the [d2rloader.developer]
                            block: Enable Console, Assert Dialog
                            Message, Enable Logging (master), plus
                            7 cascaded log-detail toggles
      Plugins             - opens the Plugin Manager (see below)
* Plugin Manager (v1.3+, extended in v1.4):
      Lists every plugin (.dll) and patch (.json) the launcher finds
      for the currently-selected mod, grouped by scope (global vs
      mod-local). Toggle any entry on or off - disabled files get
      moved to a sibling "Disabled\" folder rather than deleted, so
      you can flip them back on later. Mods that ship a
      plugin_config.json declare their own curated plugin+patch set:
      the Plugin Manager shows that list read-only and the launcher
      moves your globals out of the way while that mod is active,
      so nothing outside the author's curated list gets loaded.
* Plugin/patch friendly names (v1.3+):
      Right-click any row in the Plugin Manager and pick Rename to
      set a readable display name for that filename. Names live in a
      shared plugin_manifest.json beside the launcher and apply to
      every mod that uses that file. Same manifest covers both DLLs
      and JSON patches.
* A collapsible bottom panel (click the arrow) with quick links to modding
  references, your local tools, and tool downloads
* Live mod-folder watching - install or remove a mod and the Refresh button
  lights up to let you know there's new state to pick up


--------------------------------------------------------------------------------
  INSTALLATION
--------------------------------------------------------------------------------

1. Extract the launcher folder anywhere you like.

2. The folder should contain:
       Angiris.exe     (this launcher)
       angiris.ico
       assets\
           fonts\        (optional - see TYPOGRAPHY below)
           images\       (UI artwork: frame_main.png, bg_stone.png, etc.)
           seeds.json    (optional - see SEED ARGUMENT below)
           playtime.json (auto-created on first launch of a mod)
       README.txt      (this file)
       FAQ.txt
       CHANGELOG.txt

3. Make sure D2RLoader.exe (the loader/injector) is present in your
   Diablo II: Resurrected install folder.

4. Double-click the launcher.

No installer, no registry entries, no DLLs to copy. The launcher writes a
single config file (launcher_config.json) next to itself to remember settings,
rewrites assets\seeds.json when you save typed seed values, and updates
assets\playtime.json after each gameplay session.


================================================================================
                      Angiris Launcher  -  BUILD INSTRUCTIONS
================================================================================

The launcher source is 35 .cpp files (split by concern - UI, layout, paint,
Windows plumbing, plugin manager, loader-options modals, etc.) plus their
.h headers and one optional resource script.

Two supported build paths:

  A) MSYS2 / MinGW-w64  (UCRT64)   - the project's primary build
  B) Visual Studio 2019 or 2022    - works equally well, no source changes

Both produce a single self-contained Angiris.exe with no DLL dependencies.


--------------------------------------------------------------------------------
  FILES YOU NEED
--------------------------------------------------------------------------------

Grab the whole source folder. It contains:

    *.cpp / *.h              the launcher source
    compile_mingw.bat        the MinGW build script - knows the .cpp list
    angiris.rc               resource script (icon + version info) - optional
    angiris.ico              the launcher icon - optional, referenced by .rc

The .rc and .ico are optional: leaving them out just gives you an .exe with
the default Windows icon. Functionality is identical.

The batch script has a single SOURCES variable listing every .cpp file. If
you add or remove one, edit that one line near the top of compile_mingw.bat
and it flows to the sanity check, the display, and the g++ command.


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

Drop compile_mingw.bat in the same folder as the .cpp / .h files and
double-click it. The script:

  1. Locates g++ - either on PATH, or by scanning drives for an MSYS2 install
  2. Verifies every listed .cpp source is present in the folder
  3. Compiles angiris.rc via windres (skipped if no .rc is present)
  4. Compiles + links every .cpp into a single static Angiris.exe

About 30-60 seconds. On success it prompts to launch the result.


WHAT THE SCRIPT DOES (so you can replicate manually)
--------------------------------------------------------------------------------

If you'd rather invoke g++ yourself, the commands are:

    windres -i angiris.rc -O coff -o angiris.res.o

    g++ -O2 -std=c++17 -mwindows -municode ^
        *.cpp angiris.res.o -o Angiris.exe ^
        -static ^
        -lgdiplus -lcomctl32 -lshell32 -ladvapi32 -lversion -lbcrypt ^
        -lcomdlg32 -lshlwapi -lole32 -luuid -lwinhttp

Using *.cpp expands to every source file in the folder - simplest way to
keep the command in sync with the source list. If you'd rather list them
individually, copy the SOURCES variable from the top of compile_mingw.bat.

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
    version     - reading the file-version resource out of D2RLoader.exe
                  (the version shown in the About modal)  [added v1.4.1]
    bcrypt      - SHA-256 via Win32 CNG, used to verify the D2RLoader
                  download against the checksum published on
                  d2rloader.net  [added v1.4.1]


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
       Right-click "Source Files" -> Add -> Existing Item, then in the
            file picker open the launcher source folder and multi-select
            EVERY .cpp file (Ctrl+A picks them all).
       Right-click "Header Files" -> Add -> Existing Item, then multi-
            select EVERY .h file the same way.
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
            comdlg32.lib;shlwapi.lib;ole32.lib;uuid.lib;winhttp.lib;
            version.lib;bcrypt.lib


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

* "[ERROR] Missing source file: <path>\<name>.cpp" from compile_mingw.bat.
      The batch script's SOURCES list refers to a file that isn't in your
      folder. Either you're missing a .cpp from a partial checkout, or
      you're building an older/newer source tree than the script expects.
      Either sync the folder to a full set or edit the SOURCES line at the
      top of compile_mingw.bat to match what you have.

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


--------------------------------------------------------------------------------
  TYPOGRAPHY (OPTIONAL)
--------------------------------------------------------------------------------

The launcher uses the original Diablo II menu font (Exocet Blizzard Medium)
for headings, mod names, and labels - the authentic D2R feel - with Cinzel as
a secondary serif and Georgia for italic body text. If a font is missing the
launcher falls back to a system serif, but it looks best with the real fonts.

To install them, drop the .ttf files into assets\fonts\ :

       Exocet-Blizzard-Medium.ttf      (primary - launcher headings/labels)
       Cinzel-Regular.ttf              (secondary)
       Cinzel-Bold.ttf
       Cinzel-Black.ttf
       IMFellEnglishSC-Regular.ttf     (optional small caps)

Cinzel is free from fonts.google.com/specimen/Cinzel ("Download family").
IM Fell English SC is on the same site.

Every .ttf in assets\fonts\ also shows up in the toolbar Font dropdown so
you can switch the launcher's display font live. The launcher loads the
fonts privately at startup - they don't pollute your system font list.

When you pick a non-Exocet font, the launcher caps its rendered cell height
at Exocet's so the layout doesn't overflow (Cinzel Black at the same em
size is about 30% taller than Exocet, which used to clip mod-row banners
and the launch-command preview at high DPI).

Note: launcher self-updates do NOT replace .ttf files. Windows holds an
internal mapping for any font that's been used to render text in the current
process, which prevents the install from overwriting the active font file.
If a future launcher release ships an updated or new font, drop the new
.ttf into assets\fonts\ manually after installing the update.


--------------------------------------------------------------------------------
  FIRST RUN
--------------------------------------------------------------------------------

The launcher tries to locate your D2R install automatically. If it can't, a
message will prompt you to set it: use the "..." button next to the Loader
Directory field at the bottom-left of the window, and pick your
"Diablo II Resurrected" folder (the one containing D2R.exe / D2RLoader.exe).

Any mods in <D2R>\mods\ appear in the list in the center column. If no mods
are found, the launcher tells you once at startup.


--------------------------------------------------------------------------------
  LAYOUT OVERVIEW
--------------------------------------------------------------------------------

LEFT RAIL
    Logo, then the version label (clickable when an update is available -
    glows gold to draw your eye), then quick-access buttons:
        Mods    - opens your <D2R>\mods\ folder
        Logs    - opens your <D2R>\logs\ folder
        Help    - opens this FAQ
        About   - opens the About modal (version info, D2RLoader
                  download, README / FAQ / Discord)
        Exit    - closes the launcher
    Loader Options (bottom):
        "..."               - button next to the LOADER OPTIONS header;
                              opens the folder picker to change your D2R
                              install location
        Basic Options       - opens the Basic Options modal (Show Sockets,
                              Show Item Level, Respec Skill/Stats, Stash
                              Tabs, Material Limit, Show TCP/IP Button)
        Developer Options   - opens the Developer Options modal (Enable
                              Console, Assert Dialog Message, Enable
                              Logging + 7 log-detail toggles)
        Plugins             - opens the Plugin Manager for the currently-
                              selected mod

CENTER COLUMN
    MODS header + Refresh button
    The mod list - up to 5 rows visible, scroll for more (one row per mod).
    Right-click any row for the context menu (Backup Saves / Re-zip /
    Uninstall). Hover for two seconds for the playtime tooltip.
    Nexus Mod Directory  - opens the D2R Nexus page
    Update Selected Mod  - opens the selected mod's source page (if an
                           update is available)
    APPEARANCE TOOLBAR (under the action buttons):
        On Launch | <slider>    - 3-state Launcher behaviour when launching
                                  a mod (stay open, minimize, or close)
        Scale | <slider>        - 3-state UI size slider (DPI-aware)
        Font  | <dropdown>      - pick the display font
        Colour | <dropdown>     - pick the accent color

RIGHT COLUMN
    MOD DESCRIPTION
        The selected mod's description, with optional Discord / Docs /
        Website buttons (flat - click-shrink, no hover-grow) and an
        "Update available" bar at the bottom.
    LAUNCH OPTIONS
        Six flag checkboxes, the Seed row (checkbox + text input + dropdown
        arrow), the auto-resizing launch-command preview, and the big PLAY
        button.

BOTTOM PANEL (hidden by default - click the arrow under the center column)
    REFERENCES        - Eez's File Guides, Phrozen Keep, Amazon Basin
    TOOLS AND PROGRAMS - Edit TXT / Sprite / JSON / Particles / Textures /
                         Models (launches your local tools)
    DOWNLOADS         - AFJ Pro Text Editor, Eez's Sprite Editor,
                         Visual Basic Code

Each section has its own background sampled from a different region of
bg_stone.png, so the panels read as distinct stone surfaces rather than one
uniform tile.


--------------------------------------------------------------------------------
  HOW TO LAUNCH A MOD
--------------------------------------------------------------------------------

1. Click a mod in the list.
2. (Optional) Toggle command-line flags in the Launch Options panel.
3. (Optional) Enable Seed and pick / type a seed value.
4. Click PLAY.

The launch-command preview updates live as you toggle flags or type into
the Seed input so you can see exactly what's about to be passed to
D2RLoader.exe. The preview box auto-grows from 1 to 3 wrapped lines and
shrinks back as you remove args.

Per-mod flags + the seed value are saved automatically every time you
change them. Switch to a different mod and back, and your flags come right
back.

After launching, the launcher chooses its behaviour based on the On Launch
toggle (stay open, minimize, or close).


--------------------------------------------------------------------------------
  PLAYTIME TRACKING
--------------------------------------------------------------------------------

Every time you launch a mod and D2R actually starts up, the launcher begins
counting. When D2R exits, the elapsed time is added to that mod's cumulative
total and the "last played" date is updated.

To see the numbers, hover your cursor over any mod row for ~2 seconds. A
small tooltip appears with:

    Total playtime:  e.g. "12h 34m" or "47m" or "2d 5h 12m"
    Last played:     e.g. "today", "yesterday", "3 days ago",
                          or an actual date for older sessions

What's NOT counted:

* Time spent in the launcher itself (browsing mods, tweaking flags)
* Time between clicking PLAY and D2R actually appearing - the launcher waits
  to see D2RLoader.exe / D2R.exe come up before starting the clock
* Time after D2R exits (obviously)

The data lives in assets\playtime.json next to the launcher. It's keyed by
mod folder name, so reinstalling the same mod into the same folder keeps
its history. Deleting playtime.json resets all playtime data; deleting one
entry resets just that mod.


--------------------------------------------------------------------------------
  RIGHT-CLICK MOD MENU
--------------------------------------------------------------------------------

Right-click any mod row to open the context menu:

    Backup Saves
        Takes an immediate snapshot of this mod's save folder. The snapshot
        goes into a "backups\" subfolder next to the save data, named with
        the current timestamp (YYYY-MM-DD_HHMMSS). See SAVE BACKUPS below.

    Re-zip
        Packages the mod folder into a .zip file in your <D2R>\mods\
        directory. Useful for sharing the mod with others or for snapshotting
        a working build before making changes. The resulting zip lives
        alongside the mod folder and uses the mod's name as the filename.

    Uninstall
        Asks for confirmation, runs a save backup first (so your characters
        and stash aren't lost), then removes the mod folder from <D2R>\mods\
        entirely. The mod's playtime entry stays in playtime.json - if you
        reinstall later, your history comes back.


--------------------------------------------------------------------------------
  SAVE BACKUPS
--------------------------------------------------------------------------------

D2R writes each mod's character + stash data to:

    %USERPROFILE%\Saved Games\Diablo II Resurrected\Mods\<savepath>\

where <savepath> is the "savepath" field from the mod's modinfo.json.

A backup snapshot is a full copy of that folder into:

    %USERPROFILE%\Saved Games\Diablo II Resurrected\Mods\<savepath>\backups\<YYYY-MM-DD_HHMMSS>\

The backups\ subfolder itself is excluded from the snapshot (so we don't
recurse into the backup tree we're building).

Snapshots are taken:

* Manually   - via the right-click "Backup Saves" menu item
* Automatic  - before any drag-and-drop install that uses Overwrite mode
               (where the existing mod folder is about to be wiped)
* Automatic  - before any Uninstall

Rotation: the launcher keeps the FIVE most recent snapshots for each mod.
When you take a sixth, the oldest one is deleted. This keeps the backup
folder from growing without bound while still giving you a meaningful
history.

To restore from a snapshot, manually copy the contents of the timestamped
folder back up one level (the snapshot folder layout mirrors the live save
folder).


--------------------------------------------------------------------------------
  LAUNCHER SELF-UPDATES
--------------------------------------------------------------------------------

The launcher checks GitHub on startup to see if a newer launcher release is
available.

When an update IS found:

1. The version label in the left rail (under the logo) glows gold and
   becomes clickable.
2. Click it to open the update prompt with three buttons:
       Update          - download and install the new version in place
       Skip This Version - dismiss for this specific version (remembered;
                          the prompt won't reappear until a NEWER version
                          ships)
       Ignore          - dismiss just this once (the prompt reappears on
                          next launch)

Clicking Update:

1. The main launcher window hides (so its locked assets can be released).
2. A small popup appears showing three statuses:
       Downloading       - fetching the release zip from GitHub
       Updating          - extracting and copying new files into place
       Complete          - install done, ready to restart
3. A Restart button appears at Complete. It's disabled for half a second
   first (so an accidental click can't fire), then enables.
4. Click Restart: the new launcher version spawns and the running one closes.

What gets updated: the .exe, all PNG assets, JSON data files, and any docs
shipped in the release. What does NOT get updated: .ttf font files. See
TYPOGRAPHY above for why and what to do if you need a new font.

What if the install fails partway: the diagnostic log
(assets\last_update_install.log) records every file copy attempt. If the
Restart button does appear, the install reached the "Complete" state and
the launcher is updated; restart picks up the new version. If the popup
hangs on "Updating" forever, check the log to see what went wrong.

After restart, the previous launcher's exe lingers as Angiris.exe.old in
your install folder briefly. The new launcher deletes it on startup
automatically (retrying a few times if Windows hasn't released the file
handle yet, or scheduling it for delete on next reboot as a fallback). You
should never need to delete it manually.


--------------------------------------------------------------------------------
  SEED ARGUMENT  (-seed)
--------------------------------------------------------------------------------

The Seed row sits below the six flag checkboxes in the Launch Options panel:

    [checkbox] Seed   [text input]   [v]
                       ^^^^^^^^^^    ^^^
                       type a value  opens the seeds dropdown

The checkbox is the master switch - when off, the typed value is preserved
but -seed is NOT passed to D2RLoader. When on, the value (whatever's in the
text input) gets appended as "-seed VALUE" at the very end of the launch
command, after -skiplogovideo.

TEXT INPUT
    Digits only. Letters, separators, decimal points - all filtered out as
    you type or paste. Maximum 12 digits.

    Click in the input to position the caret at the click point. Click +
    drag to select a range. Standard keys all work:

        Left / Right            move caret
        Home / End              jump to start / end
        Backspace / Delete      remove a character (or the selection)
        Shift + Left / Right    extend selection
        Shift + Home / End      select to start / end
        Ctrl+A                  select all
        Ctrl+C                  copy
        Ctrl+X                  cut
        Ctrl+V                  paste (digits-only filter applies)
        Enter                   commit and leave the input
        Esc                     leave without saving to recents

DROPDOWN
    Click the [v] arrow to open the seeds list. Recents show first (newest
    at top, labeled "Recent3", "Recent2", "Recent1"), then any presets from
    seeds.json. Picking an entry fills the text input with that value and
    auto-enables the checkbox.

SEEDS.JSON
    The dropdown's contents come from assets\seeds.json. The file has two
    arrays - one for presets you maintain by hand, one for the launcher's
    rolling 3-slot recents history:

        {
          "seeds": [
            {"name": "Speedrun 1.13c", "value": "1234567"},
            {"name": "Holy Grail Run", "value": "999"}
          ],
          "recent": [
            {"name": "Recent1", "value": "111"},
            {"name": "Recent2", "value": "222"},
            {"name": "Recent3", "value": "333"}
          ]
        }

    The launcher only rewrites the "recent" array - everything in "seeds"
    passes through untouched, so you can curate that list freely. If
    seeds.json is missing or has no "seeds" array, the launcher falls back
    to a small built-in starter list so the dropdown still works.

RECENTS LOGIC
    Type a value, commit it (press Enter, click outside, or open the
    dropdown), and the value is appended to "recent" - oldest slot first
    falls off:

        before:  Recent1=A  Recent2=B  Recent3=C   (C is newest)
        type D:  Recent1=B  Recent2=C  Recent3=D   (A fell off)

    Values that already match a preset or an existing recent are NOT
    duplicated. Picking from the dropdown doesn't create a Recent entry
    either (it's already in the list).


--------------------------------------------------------------------------------
  APPEARANCE TOOLBAR  (SCALE / FONT / COLOUR)
--------------------------------------------------------------------------------

The toolbar sits under the action buttons in the center column.

SCALE
    A 3-state slider. The percentages depend on your Windows display scaling:

        Windows at 100%   ->  100% / 115% / 127%
        Windows at 150%+  ->   75% /  85% / 100%

    The text box above the slider always shows the current percentage. Click
    the slider to cycle through the three values; the UI resizes immediately
    and the window resizes to match.

    If you change Windows display scaling between sessions, the launcher
    notices on next start and resets the UI scale to 1.0 - this prevents a
    setting that looked right at 100% from leaving the window off-screen at
    150%.

FONT
    Dropdown of all .ttf files in assets\fonts\, abbreviated for display
    (e.g. "Cinzel-Bold" -> "Cin-Bol"). Pick any of them and the launcher
    swaps the display font live - mod-row names, headings, button labels,
    PLAY. Italic body text (mod descriptions, hero meta) keeps Georgia
    regardless, because most display fonts don't ship an italic face.

    The launcher caps each font's rendered cell height at Exocet's so taller
    families (Cinzel Black, etc.) don't overflow the layout. If you still
    see clipping, try a smaller Scale preset.

COLOUR
    Dropdown of 8 accent colors:

        Black        Dark Red      Bright Red    Dark Green
        Bright Green Gold          Bright Gold   Pale Gold

    Drives every gold-token paint site (text, chevrons, highlights). The
    engraved frame, gem ornaments, and stone backgrounds are part of the
    artwork itself and stay bronze regardless of the accent color.


--------------------------------------------------------------------------------
  MOD METADATA (FOR MODDERS)
--------------------------------------------------------------------------------

The launcher reads each mod's modinfo.json. Standard D2R fields like name and
savepath are respected. The launcher also recognizes these optional fields:

    {
      "name":        "ModName",
      "savepath"     "ModName\"
      "title":       "Mod Name",
      "version":     "2.4.1",
      "author":      "Author Amazing",
      "description": "A complete overhaul - new acts, classes, and endgame",
      "banner":      "Banner.jpg",
      "docs":        "./docs/manual.pdf",
      "website":     "https://example.com/MyMod",
      "discord":     "https://discord.gg/abc123",
      "update_github":   "owner/repo",
      "update_manifest": "https://example.com/version.json"
    }

* title:       overrides "name" for the row + Mod Description (longer/prettier)
* version:     shown in the text fallback on the mod row
* author:      shown in the text fallback on the mod row
* description: shown in the Mod Description panel
* banner:      filename of an image in the mod's "Launcher Files\" folder.
               See BANNERS below.
* docs:        shows a "Docs" button in Mod Description (placed above
               Discord). URL or path. The older field name "documents"
               is also accepted.
* website:     shows a "Website" button. URL or path.
* discord:     shows a "Discord" button. URL or path.
* update_*:    opt-in update checking. See UPDATES below.

If a field is missing, the corresponding button just doesn't appear.


--------------------------------------------------------------------------------
  BANNERS
--------------------------------------------------------------------------------

Each mod row can display a custom banner image as its background:

* The banner fills the mod row (96 x 600) using "cover" semantics:
  the image scales to fill the row, cropping any overflow.
* For best results provide a wide image; the row is much wider than it is
  tall, so tall/square images will be cropped top and bottom.
* You can bake the mod name, author, and version directly into the banner
  art if you want full control over how the row looks.
* Format:   JPG or PNG (any GDI+-readable format)
* Location: <mod folder>\Launcher Files\<filename>
* Reference in modinfo.json: just the filename, e.g. "banner": "Banner.jpg"

If no banner is provided, the launcher renders a clean text fallback: the
mod's row becomes transparent (the panel's stone background shows through),
with the mod name on top and "by Author    v X.Y.Z" on a second line (the
version sits at the row's horizontal midpoint). No hard dark box around it.


--------------------------------------------------------------------------------
  UPDATES (FOR MODS)
--------------------------------------------------------------------------------

A mod can opt in to update checking by adding one of:

    "update_github":   "owner/repo"               (uses GitHub releases)
    "update_manifest": "https://.../version.json"  (a small JSON you host)

When an update is detected:
* A gold up-arrow badge appears on the mod's row.
* The Mod Description panel shows an "Update available: vX -> vY" bar with a
  "[Details]" link.
* Clicking "[Details]" (or "Update Selected Mod") opens the mod's source page.

The launcher does NOT download or install mod updates automatically. It only
points you at the source so you can review and install the update yourself.
This is a deliberate safety choice - the launcher can't verify the integrity
of files it didn't build.

(Note: the LAUNCHER ITSELF can self-update - see LAUNCHER SELF-UPDATES above.
The launcher's own integrity is verifiable because it's a single trusted
source, but mods come from many authors so the policy differs.)


--------------------------------------------------------------------------------
  DOCS / WEBSITE / DISCORD LINKS
--------------------------------------------------------------------------------

These three optional fields support both web URLs and local files. The launcher
auto-detects what you've given it:

  https://example.com/docs                  -> opens in your default browser
  steam://run/12345                          -> opens in Steam
  mailto:author@example.com                  -> opens in your default mail client
  C:\D2R\mods\RoK\manual.pdf                 -> opens with the .pdf association
  \\server\share\readme.txt                  -> opens from network share
  ./docs/manual.pdf                          -> opens <mod folder>\docs\manual.pdf
  manual.pdf                                 -> opens <mod folder>\manual.pdf

When using JSON, escape backslashes (\\) or use forward slashes:

    "docs":    "./docs/manual.pdf"
    "website": "C:/D2R/mods/RoK/index.html"

The three buttons are deliberately flat - they have a click-shrink press
animation (so you see the press register) but NO hover-grow. They share the
flat-press style with the title-bar minimize/close buttons and the Refresh
button. The more prominent Nexus and Update Selected Mod buttons keep their
hover-grow.


--------------------------------------------------------------------------------
  ABOUT MODAL & D2RLOADER UPDATES
--------------------------------------------------------------------------------

The About button in the left rail opens a themed modal rather than
launching README.txt in Notepad (which is what it did before v1.4.1).

  HUB VIEW
  --------

      Angiris  v1.4.1
      D2RLoader  v1.0.1 - beta        (or "Not detected")

      [   Latest D2RLoader   ]        <- download + install button

        (README)   (FAQ)   (Discord)  <- square icon buttons

                [ Close ]

  The D2RLoader version comes from the file-version resource inside
  D2RLoader.exe. If the exe is missing, or a particular build doesn't
  carry a version stamp, the line reads "Not detected" rather than
  showing a blank or a guess.

  README and FAQ open in a scrollable reader inside the modal - scroll
  with the mouse wheel. From a document, Close returns you to the hub;
  from the hub, Close dismisses the modal. Esc does the same at each
  step. Discord opens the D2RLoader community server in your browser.


  DOWNLOADING D2RLOADER
  ---------------------

  "Latest D2RLoader" fetches the current release from d2rloader.net and
  installs it into your D2R folder. Progress shows in the modal:

      Downloading -> Verifying -> Extracting -> Installing -> Updating config

  Close the game first. If D2R or D2RLoader is running the launcher will
  say so and offer to close them for you - their open file handles would
  otherwise make the install fail partway through. Choosing "Yes" ends
  both processes and continues automatically. The launcher checks again
  right before writing any file, so starting the game mid-download
  aborts cleanly instead of leaving a half-updated install.

  A full log of every step lands in assets\d2rloader_install.log.


  DOWNLOAD VERIFICATION
  ---------------------

  d2rloader.net publishes a SHA-256 checksum for each release. Before
  extracting anything, the launcher hashes what it downloaded and
  compares it against that published value.

  * If the hashes DON'T match, the install stops and nothing is written
    to your game folder. A mismatch means the file isn't what the
    official release should be - a truncated download, a stale cached
    copy, or tampering.
  * If the checksum can't be read (site unreachable, page layout
    changed), the install continues and notes it in the log. The
    download itself still travels over HTTPS.

  You can check manually too - the same page lists a PowerShell command
  for it, and the launcher's log records both hashes side by side.


  "D2RLOADER UPDATE AVAILABLE"
  ----------------------------

  At startup the launcher compares the D2RLoader you have installed
  against the current release on d2rloader.net. If yours is older, gold
  "D2RLoader Update Available" text appears above the LOADER OPTIONS
  header. Click it to open the About modal and update.

  The notice appears ONLY when both versions are known and the site's is
  actually newer. If D2RLoader isn't installed, the check fails, or
  you're already current, nothing is shown - it won't claim an update it
  can't verify. Pre-release tags are ignored when comparing, so 1.0.1
  and 1.0.1-beta count as the same version.

  Details go to assets\d2rloader_check.log.


  YOUR D2RLOADER.TOML IS NEVER OVERWRITTEN
  ----------------------------------------

  Installing a D2RLoader update preserves your existing D2RLoader.toml
  and amends it instead:

  * Settings you already have keep their values. If you set
    show_ground_sockets = true and the new release ships false as the
    default, you keep true.
  * Keys and sections that are genuinely new get appended at the end
    under a marker comment:

        # ---- Added by Angiris Launcher (new D2RLoader keys) ----

  * Nothing existing is edited, reordered, or removed.
  * The original is copied to D2RLoader.toml.bak before anything is
    written.

  The appended block re-states its section headers, so you may see a
  section name appear twice in the file. That's valid TOML - parsers
  merge duplicate section headers - and it's the price of never
  touching a line you already had.


--------------------------------------------------------------------------------
  LOADER OPTIONS (D2RLoader.toml)
--------------------------------------------------------------------------------

The bottom-left LOADER OPTIONS panel is a three-button stack:

    Basic Options      - opens a themed modal with six [d2rcore.*] and
                         [d2rloader] settings
    Developer Options  - opens a themed modal with the [d2rloader.developer]
                         block (10 settings including a master log toggle
                         and 7 cascaded log-detail sub-toggles)
    Plugins            - opens the Plugin Manager (see next section)

The "..." button in the section header opens a folder picker for your D2R
install location. That path lives in launcher_config.json (in the launcher
folder), not in D2RLoader.toml.

  BASIC OPTIONS
  -------------

    Show Sockets          -> [d2rcore.items]  show_ground_sockets   (true/false)
    Show Item Level       -> [d2rcore.items]  display_item_levels   (true/false)
    Respec Skill/Stats    -> [d2rcore.player] enable_respec         (true/false)
    Stash Tabs            -> [d2rcore.stash]  add_shared_tabs       (0-16 dropdown)
    Material Limit        -> [d2rcore.stash]  set_materials_limit   (0-255 text box)
    Show TCP/IP Button    -> [d2rloader]      show_tcpip_button     (true/false)

  Material Limit is a text box because 0-255 is too wide for a dropdown.
  A "Default = 99, Max = 255" note under the label documents the range.
  Values above 99 are D2RLoader-documented as producing undefined behaviour
  even though the maximum accepted is 255. Out-of-range typing snaps to
  the nearest bound when the field loses focus.

  DEVELOPER OPTIONS
  -----------------

    Enable Console            -> [d2rloader.developer] enable_console
    Assert Dialog Message     -> [d2rloader.developer] assert_dialog_mode
    Enable Logging            -> [d2rloader.developer.logs] enabled  (master)
      JSON Resources          -> [d2rloader.developer.logs] json_resources
      Widget Panel Creation   -> [d2rloader.developer.logs] widget_panels
      Excel File Loaded       -> [d2rloader.developer.logs] excel_files
      True and Open Type Fonts-> [d2rloader.developer.logs] fonts
      UI Sprites Creation     -> [d2rloader.developer.logs] sprites
      Chat Messages           -> [d2rloader.developer.logs] chat_messages
      Models Creation         -> [d2rloader.developer.logs] models

  The 7 indented log-detail toggles are inert (greyed-out) when Enable
  Logging is off. Flipping the master to on makes them live; flipping
  it back off greys them again — their individual values are preserved
  in the toml either way, D2RLoader just ignores them without the
  master.

  APPLY-IMMEDIATELY
  -----------------

  Both modals apply changes to D2RLoader.toml on every click - there is
  no Save / Cancel. D2RLoader only reads the toml at launch time, so
  there's no in-session state to preserve or roll back. Just close the
  modal when you're done.

  LAUNCHER-OWNED KEYS
  -------------------

  On every launcher start, two toml keys are force-reset:

    [d2rloader] default_mod       = ""
    [d2rloader] skip_title_screen = false

  Setting default_mod would tell D2RLoader to auto-launch a specific
  mod, skipping Angiris's mod picker entirely; skip_title_screen would
  bypass the D2R title screen. The launcher resets these so it stays
  the entry point. You can hand-edit them between sessions, but the
  launcher will reset them again next start.

  These are the only keys Angiris touches without a user action. Every
  other setting in the toml is left exactly as you (or a hand-editor)
  left it - even keys the launcher doesn't know about.


--------------------------------------------------------------------------------
  PLUGIN MANAGER (v1.3+, extended in v1.4)
--------------------------------------------------------------------------------

D2RLoader-compatible mods can ship .dll plugins that hook into the game
and .json memory patches that modify runtime data. The Plugin Manager
(bottom-left "Plugins" button) shows every plugin AND every patch the
launcher finds for the currently-selected mod, split into two groups:

    Global plugins  - live in <D2R>\d2rloader\plugins\
                    and <D2R>\d2rloader\patches\
                    available to every mod
    Mod plugins     - live in <D2R>\mods\<Mod>\d2rloader\plugins\
                    and <D2R>\mods\<Mod>\d2rloader\patches\
                    mod-local only

Each row has a checkbox. Toggling an entry off moves its file into a
sibling "Disabled\" folder inside plugins\ or patches\ (whichever is
appropriate for its extension) - nothing is deleted. Toggling back on
moves it out of Disabled.

Save Selection commits your changes; Cancel closes the window without
saving.

Right-click any row and pick Rename... to set a friendly display name.
Names are stored in plugin_manifest.json beside the launcher and apply
to every mod that references that filename. The Plugin Manager shows
entries as "Friendly Name (filename.dll)" or "Friendly Name (filename.json)"
once a name is set - the file extension in parentheses tells you whether
it's a plugin or a patch.


  PER-MOD CONFIGS (plugin_config.json)
  ------------------------------------

  Mod authors can ship a plugin_config.json file inside the mod's root
  folder (<D2R>\mods\<ModName>\plugin_config.json) that declares exactly
  which plugins AND patches the mod is designed to run with. Format:

      {
          "plugins": [
              "someplugin.dll",
              "someplugin_fix.json",
              "anotherplugin.dll"
          ]
      }

  DLL and JSON entries may be mixed in the single "plugins" array —
  the launcher routes each to the correct destination folder by extension.

  When a mod ships one:

  * The Plugin Manager shows the authored list in read-only inventory mode
    (no toggles). Missing entries are greyed out.

  * All globally-enabled plugins AND patches get moved to their respective
    <D2R>\d2rloader\<plugins|patches>\Disabled\ folders for that mod's
    session, so nothing outside the author's list can load.

  * Any entry listed in the config but missing from the mod's local
    folder is auto-recovered from the global folders or the mod's
    Disabled folder if it exists there. Globals are COPIED (so other
    mods still have them); mod-local Disabled entries are MOVED.

  * The same auto-recovery + globals-to-disabled sweep runs the moment
    you press PLAY, so switching between mods can never leave stale
    globals enabled at launch time.

  An empty plugin list ({"plugins": []}) is a valid way for an author to
  say "this mod ships without plugins or patches" - the Plugin Manager
  will show a centred message ("<Author> has selected to disable plugins
  for <Mod>") and your globals will still be swept aside for the session.


  PLUGIN NAMES (plugin_manifest.json)
  -----------------------------------

  plugin_manifest.json lives beside Angiris.exe and stores friendly
  display names for both plugin DLLs and JSON patches. Format:

      {
          "names": {
              "someplugin.dll":       "Some Plugin",
              "someplugin_fix.json":  "Some Plugin (memory fix)",
              "anotherplugin.dll":    "Another Plugin"
          }
      }

  Two ways to edit:

  * Right-click a plugin/patch row in the Plugin Manager and pick Rename...
  * Open plugin_manifest.json in a text editor.

  Whenever the Plugin Manager closes, the launcher rewrites the file with
  any newly-discovered files (plugins or patches) added as empty entries -
  so opening the manifest in a text editor gives you a ready-made list of
  every filename the launcher knows about, waiting for you to type in
  names. Names are matched case-insensitively.

  A curated default plugin_manifest.json ships with the launcher.


--------------------------------------------------------------------------------
  TOOLS (BOTTOM PANEL)
--------------------------------------------------------------------------------

The TOOLS AND PROGRAMS section launches your locally-installed modding tools:

    Edit TXT Files    -> your TXT/Excel editor (e.g. AFJ Sheet Editor Pro)
    Edit Sprite Files -> your sprite editor
    Edit JSON Files   -> your JSON/text editor (e.g. VS Code)
    Edit Models       -> your model editor
    Edit Textures     -> your texture editor
    Edit Particles    -> your particle editor

The first time you click a tool button, the launcher searches your tools folder
for a matching .exe (resolving .lnk shortcuts), then caches the path so later
clicks are instant. If it can't find the tool, you'll be prompted to locate it
manually; your choice is cached.


--------------------------------------------------------------------------------
  CONFIG FILES
--------------------------------------------------------------------------------

GLOBAL (next to the launcher exe):
    launcher_config.json
        d2r_path                  - your D2R install path
        last_mod                  - last-selected mod (auto-restored on launch)
        tools_dir                 - your modding tools directory
        tool_excel                - cached TXT/Excel editor path
        tool_strings              - cached JSON/text editor path
        tool_sprite               - cached sprite editor path
        tool_models               - cached model editor path
        tool_textures             - cached texture editor path
        tool_particles            - cached particle editor path
        launch_behavior           - what the launcher does after PLAY
                                    (default: minimize)
        ui_scale                  - 0.75/0.85/1.0/1.15/1.27 - picked via the
                                    Scale slider
        font_name                 - chosen display font (filename stem, e.g.
                                    "Cinzel-Bold")
        font_color                - index into the 8-color presets, -1 = default
                                    Gold
        last_dpi_scale            - last-seen system DPI; used to detect a
                                    between-session DPI change and reset
                                    ui_scale if so
        skipped_launcher_version  - if you clicked "Skip This Version" on a
                                    launcher update prompt, the version you
                                    skipped is recorded here

GLOBAL (next to the launcher exe, under assets\):
    user_layout.json
        Optional. Overrides parts of the UI layout. Every field is
        optional - omit one and the launcher's default applies.

        mod_row_height        - height in px of each row in the mod list
        version_label_x       - x offset of the version label under the logo
        version_label_y       - y offset of the version label
        show_modding_expand   - true/false; false hides the expand arrow
                                that opens the bottom modding area
        scale_as_dropdown     - true/false (default false). true turns the
                                toolbar's UI Scale control from a 3-state
                                cycle toggle into a dropdown listing every
                                preset available at your DPI, laid out with
                                a "SCALE" header above the value box (like
                                the On Launch column). One click to any
                                scale instead of cycling.
        nav_buttons           - per-button overrides keyed by id ("mods",
                                "logs", "help", "about", "exit"), each
                                accepting "visible" and "enabled" booleans

        Example:
            {
                "scale_as_dropdown": true,
                "show_modding_expand": true
            }

        This file is PRESERVED across launcher updates (see below), and a
        startup migration adds any newly-introduced fields with their
        defaults without disturbing what you've already set.

    seeds.json
        seeds   - array of {name, value} presets you maintain by hand
        recent  - array of up to 3 {name, value} entries; the launcher writes
                  this when you commit a typed seed value (newest = Recent3)
        The launcher only rewrites the "recent" array; "seeds" passes through.

    playtime.json
        Keyed by mod folder name. Each entry records cumulative seconds played
        and the last-played date. Survives mod reinstalls (because the key is
        the folder name, not anything inside the mod).

  FILES PRESERVED ACROSS LAUNCHER UPDATES
  ---------------------------------------

  Updating the launcher will NOT overwrite these, because they hold data
  you created rather than defaults the launcher ships:

      assets\user_layout.json
      assets\seeds.json
      assets\playtime.json

  On a first-time install (where the file doesn't exist yet) the bundled
  default is written normally. On an update, an existing file is left
  exactly as it is.

PER-MOD (inside each mod folder, under "Launcher Files\"):
    launcher_mod_cfg.json
        no_sound        - true/false
        windowed        - true/false
        use_txt         - true/false (locked on)
        skip_intro      - true/false
        respec          - true/false
        reset_maps      - true/false
        use_seed        - true/false (whether -seed is appended)
        seed_arg        - the numeric value for -seed (kept even when use_seed
                          is false, so toggling the checkbox doesn't lose it)

LOADER (in your D2R install folder):
    D2RLoader.toml      - the launcher edits only the keys it surfaces
                          via the Basic Options and Developer Options
                          modals, plus default_mod / skip_title_screen
                          (force-reset on startup). Everything else in
                          the toml is left alone.

DIAGNOSTIC LOGS (next to the launcher exe, under assets\):
    last_update_check.log    - what the launcher saw the last time it asked
                               GitHub about its own version. Overwritten each
                               check.
    last_update_install.log  - per-file copy results from the last launcher
                               self-update install. Appended each run, so you
                               can see history. Useful if a self-update misses
                               a file.

The JSON files are plain text and safe to edit by hand, but the launcher
overwrites them on every change. seeds.json updates only when a typed value
gets committed to recents; the rest of its contents pass through unchanged.


--------------------------------------------------------------------------------
  KEYBOARD / MOUSE
--------------------------------------------------------------------------------

GENERAL
* Click any mod row to select it
* Right-click any mod row for the context menu (Backup / Re-zip / Uninstall)
* Hover any mod row for ~2 seconds to see the playtime tooltip
* Mouse wheel scrolls the mod list (or use the custom scrollbar on the right
  edge of the list)
* Click the arrow under the center column to show/hide the bottom panel
* Click the version label (left rail) when it glows gold to open the launcher
  update prompt
* Standard window controls (minimize / close) in the title bar
* Alt+F4 closes (Windows default)

SEED INPUT (when the text input has focus - click into it first)
* Digits 0-9         - type at the caret (everything else is filtered out)
* Left / Right       - move caret
* Home / End         - jump to start / end
* Backspace / Delete - remove a character (or the selection)
* Shift + arrow      - extend selection
* Shift + Home / End - select to start / end
* Click              - position caret at the click point
* Click + drag       - drag-select a range
* Ctrl+A             - select all
* Ctrl+C             - copy selection (or whole value)
* Ctrl+X             - cut
* Ctrl+V             - paste from clipboard (digits-only filter applies)
* Enter              - commit + leave the input (saves to seeds.json recents)
* Esc                - leave without committing to recents


--------------------------------------------------------------------------------
   HOW TO SET UP MY MOD FOR UPDATE CHECKS
--------------------------------------------------------------------------------


D2RLoader Mod Update System — Guide for Mod Authors
What this does
D2RLoader can automatically check whether your mod has a newer version available and show an "update available" indicator (gold ↑ badge) on the mod entry in the launcher. Users can then click Update Selected Mod to fetch the latest release.
The system is opt-in — if you don't set it up, your mod still works fine in the launcher, users just won't get update notifications.
How it works at a glance

You publish your mod's release info (a manifest file) at a stable URL.
The launcher periodically fetches that URL and compares the version field against the locally-installed version.
If the remote version is newer, the launcher flags the mod as updatable.
When the user clicks Update, the launcher downloads the release archive from a URL specified in the manifest.

What you need to set up
1. A manifest file hosted at a stable URL
Create a small JSON file that describes your mod's current release. Host it somewhere with a permanent URL — GitHub raw, Nexus Mods, your own site, anything stable. Example:
json{
  "name": "Warlock's Reckoning",
  "version": "1.4.2",
  "released": "2026-05-12",
  "download_url": "https://github.com/yourname/warlocks-reckoning/releases/download/v1.4.2/warlocks-reckoning-1.4.2.zip",
  "changelog_url": "https://github.com/yourname/warlocks-reckoning/releases/tag/v1.4.2",
  "min_loader_version": "0.5.0"
}
Required fields:

name — the mod's display name (must match the folder name or the name field in your mod's local config)
version — semantic version string (MAJOR.MINOR.PATCH)
download_url — direct URL to the release archive (.zip recommended)

Optional but recommended:

released — ISO date of the release (used for display, sorting)
changelog_url — link the user can open to read what changed
min_loader_version — minimum D2RLoader version required (the launcher skips the update if it's too old)

2. Include a local version file in your mod
Inside your mod folder, ship a modcfg.json (or equivalent — see your mod's existing config) that includes the current version:
json{
  "name": "Warlock's Reckoning",
  "version": "1.4.2",
  "update_manifest_url": "https://raw.githubusercontent.com/yourname/warlocks-reckoning/main/manifest.json",
  "discord_url":  "https://discord.gg/yourserver",
  "docs_url":     "https://github.com/yourname/warlocks-reckoning/wiki",
  "website_url":  "https://yourmodsite.com"
}
The critical fields for updates are:

version — the version this installed copy of the mod represents
update_manifest_url — where the launcher should fetch the manifest from

The launcher reads update_manifest_url, fetches that URL, parses the JSON, and compares the remote version to the local version.
The Discord / Docs / Website URLs are unrelated to updates but power the link buttons shown in the Mod Description panel.
Versioning rules
D2RLoader compares versions using semantic versioning (MAJOR.MINOR.PATCH):

1.4.2 is newer than 1.4.1
1.5.0 is newer than 1.4.9
2.0.0 is newer than 1.99.99

Pre-release suffixes (1.4.2-beta, 1.4.2-rc1) are treated as older than the same version without the suffix. So 1.4.2-beta < 1.4.2.
If your version string can't be parsed as semver, the launcher falls back to a lexicographic compare — works for simple numeric versions but breaks down for anything fancy. Stick to semver.
Update workflow from the user's perspective

User opens the launcher. The launcher checks each installed mod's update_manifest_url once per session (respecting a TTL cache, so it doesn't spam your server).
If the remote version is newer than the local one, the mod row shows a gold ↑ indicator.
User selects the mod. The "Update Selected Mod" button becomes active in the center column.
User clicks Update. The launcher:

Backs up the current mod folder (to <mod>.backup)
Downloads the release archive from download_url
Verifies the archive is a valid ZIP
Extracts it over the mod folder
Reads the new modcfg.json to confirm the version updated
Deletes the backup on success, or restores it on failure



What you DON'T need to do

You don't need to register your mod with anyone — D2RLoader is fully decentralized
You don't need to ship the manifest with your mod — it lives at a URL, not in the mod folder
You don't need to host the archive on any specific platform — any URL the launcher can reach with HTTPS works

Recommended setup pattern
The cleanest pattern is:

GitHub releases for hosting the archive and manifest
The manifest URL points to a stable file (e.g. main/manifest.json)
Your release process:

Tag a new version in git
Build the release .zip
Upload it to GitHub Releases
Update manifest.json in your repo (bump version, update download_url, released)
Commit and push


Users running the launcher within the next TTL window get the update prompt

This way the manifest URL is stable forever, only the contents change.
Things to keep in mind

HTTPS only. The launcher refuses plain HTTP for both the manifest fetch and the archive download.
Archive format. ZIP is supported. Other formats (7z, tar.gz, rar) may not work — stick to ZIP for compatibility.
Archive layout. The archive should extract directly into the mod folder. Don't wrap everything in a top-level directory like warlocks-reckoning-1.4.2/ — the launcher unzips contents into the existing mod folder.
Don't break modcfg.json field names between versions. The launcher reads the new copy after extraction to confirm the update; if the field names change, the launcher may treat it as a corrupt install.
Test before publishing. Spin up a manifest pointing to a 1.0.1 archive while your local mod is at 1.0.0 and verify the launcher detects, downloads, and applies the update cleanly.

Failure modes
If something goes wrong during update:

Manifest unreachable: launcher silently skips the update check, no error shown to the user
Manifest malformed: launcher logs a warning, skips
Version field missing or unparseable: launcher skips
Archive download fails: the in-progress update aborts, backup is restored
Archive is corrupt: abort + restore
min_loader_version exceeds the user's launcher version: launcher shows the update is available but disables the Update button, suggesting the user update D2RLoader first

Minimal viable setup
If you just want the simplest possible thing:

Add version and update_manifest_url to your mod's modcfg.json
Host a manifest somewhere with name, version, and download_url
Done

Everything else (changelog URL, min loader version, release date) is optional polish

--------------------------------------------------------------------------------
  TROUBLESHOOTING
--------------------------------------------------------------------------------

See FAQ.txt for common issues. The big ones:

* Can't find D2R   -> use the "..." button (bottom-left) to set the Loader
                      Directory manually
* Mod won't launch -> make sure D2RLoader.exe exists in your D2R folder
* Tools won't launch -> click the tool button and locate the exe the first time
* UI clipped after font swap -> try a lower Scale preset, or revert to Exocet
                                via the Font dropdown
* Seed not being passed -> the checkbox in the Seed row is the master switch;
                           having a value typed doesn't enable -seed by itself
* Launcher update popup hangs -> check assets\last_update_install.log for the
                                 file that failed to copy
* Angiris.exe.old left over -> the next launch deletes it; if not, check the
                               install log for the error code

If something else is wrong, check launcher_config.json and the per-mod
launcher_mod_cfg.json files; they're plain JSON and easy to inspect.


--------------------------------------------------------------------------------
  CREDITS
--------------------------------------------------------------------------------

* Exocet Blizzard Medium - Diablo II's original menu font (Blizzard
                           Entertainment)
* Cinzel font            - by Natanael Gama (SIL Open Font License)
* IM Fell English SC     - by Igino Marini (SIL Open Font License)
* Built with MinGW-w64, GDI+, Win32
* Claude LLM - Launcher completely coded with Claude Code.

================================================================================
