# MilkDrop 2 Visualization Library Notes

## Release History

### 0.2.1.12 - 2026-04-17

Crash-stability and overlay behavior follow-up for the foobar2000 x64 component.

- Guarded album-art sprite replacement with the render lock to avoid racing the render thread during rapid track changes.
- Kept explicit album-art menu changes on a blocking lock path while allowing automatic track-change artwork updates to skip safely if rendering is busy.
- Added render-lock protection around left-click play/pause status text.
- Prevented double-click fullscreen actions from also firing the delayed single-click play/pause action.
- Restored `Playing` and `Paused` to the same large animated center text path used by track-title animations.
- Prevented automatic track-title updates from immediately overwriting `Playing` and `Paused`, while keeping suppressed title changes pending so the new title can still animate afterward.
- Kept blacklist replacement preset loading on a blocking render-lock path so `Never Show Again` reliably advances to a replacement preset.
- Scoped the Space hard-cut hotkey to foobar activation so it works in MilkDrop without stealing Space from other Windows applications.

### 0.2.1.11 - 2026-04-17

Small UI cleanup release for the foobar2000 x64 component.

- Removed the `Restore F-key overlay states on startup` preference and its persistence path.
- Kept F-key/runtime overlays on their default startup state instead of restoring saved overlay state.
- Changed the `F3` track-time overlay to display elapsed time as `m:ss` or `h:mm:ss`, matching the track length format without fractional seconds.

### 0.2.1.10 - 2026-04-17

Crash-stability and title-display hotfix for the foobar2000 x64 component.

- Reduced render-thread lock contention by making most UI-triggered plugin operations skip when the render lock is busy, while keeping explicit user actions that must complete on a blocking path.
- Reworked preset scanning and preset selection so the shared preset list is copied, sorted, and read under safer critical-section boundaries.
- Fixed preset cleanup ordering so the preset update thread is cancelled before the shared critical section is destroyed.
- Added guards around preset rating updates, text rendering, help text setup, and DirectWrite layout creation to avoid null-pointer crashes during shutdown, resize, or device transitions.
- Fixed `Show Track Title` and the `T` shortcut so they refresh the current foobar track title before launching the center title animation.
- Added a centered one-shot title fallback for cases where the title texture cannot be rendered, while suppressing the fallback when the normal large animated title succeeds.
- Kept the `F2` lower-corner title overlay independent from the one-shot center title display.
- Fixed `Never Show Again` so the replacement preset load waits for the render lock and reliably advances after the current preset is blacklisted.

### 0.2.1.9 - 2026-04-16

Help-overlay and stability follow-up for the foobar2000 x64 component.

- Reworked the built-in help overlay to use a stable left-anchored two-column `key: action` layout.
- Loaded the embedded help text by exact resource size to avoid stray trailing characters.
- Restored direct `Space`, `F2`, and `F3` shortcut behaviour after the focus-hotkey changes.
- Added crash diagnostics and minidump logging under the foobar2000 profile `milkdrop2\crashlogs` folder.
- Added safer locking around runtime preset operations and safer random-preset dispatch from the blacklist manager.
- Added a photosensitivity warning to the README.

### 0.2.1.8 - 2026-04-15

Blacklist and mouse-control usability release for the foobar2000 x64 component.

- Added persistent preset blacklist support to the foobar context menu with `Never Show Again`.
- Added a blacklist manager in preferences with direct file opening and preset-folder browsing for adding entries.
- Fixed the blacklist manager lifetime/ownership issues that could crash or hang foobar when closing preferences.
- Added a startup option to restore F-key overlay states, while keeping the default startup state reset for transient overlays.
- Updated the built-in help overlay text/layout for the newer mouse controls and clearer playback shortcut guidance.
- Refined single-click play/pause so the first click after focus regain or a context menu shows `Click Again for Play/Pause` instead of pausing immediately.
- Improved fullscreen topmost handling so fullscreen mode reapplies window ordering more reliably.

### 0.2.1.5 - 2026-04-14

Hotfix release for song title rendering in the foobar2000 x64 component.

- Scaled song titles down to the largest size that fits the title texture instead of relying on a fixed oversize render.
- Kept `...` clipping as a last-resort fallback when a title is still too wide at the minimum allowed size.

### 0.2.1.4 - 2026-04-14

Follow-up hotfix release for wait-string rendering and cursor safety in the foobar2000 x64 component.

- Fixed wide-text wait-string selection rendering writing the closing bracket into the wrong display buffer.
- Fixed wait-string display helper loops that could underflow when the cursor or selection started at position 0.
- Fixed code-mode cursor column calculation reading before the start of the edit buffer.

### 0.2.1.3 - 2026-04-14

Hotfix release for the foobar2000 x64 component focused on editor safety and album art persistence.

- Fixed the foobar-side album art toggle so it now persists correctly across restart.
- Fixed MilkDrop code-string writeback to use the correct destination capacity when accepting edits.
- Reworked the wait-string editor to use explicit narrow code storage instead of aliasing preset code through a `wchar_t` buffer.
- Kept the previous startup-state consistency fixes, including the runtime-only handling for transient preset lock and overlay state.

### 0.2.1.2 - 2026-04-14

Follow-up hotfix release for state and preference consistency in the foobar2000 x64 component.

- Stopped code-driven preset lock state from being serialized as if it were a user preference.
- Removed the remaining foobar-side read path for legacy INI-backed song title and song time overlay state.
- Reduced the chance of stale runtime state reappearing across restarts or UI state changes.

### 0.2.1.1 - 2026-04-14

Small hotfix release for the independent x64 maintenance branch.

- Stopped song title and song time overlays from appearing on startup.
- Kept `F2` / `F3` as runtime-only toggles instead of persisted startup state.
- Fixed the local Visual Studio project reference for the foobar2000 component client so the x64 build works from the current repo layout.

### 0.2.1.0 - 2026-04-13

First public standalone release from the independent maintenance repository.

- Added a public x64 GitHub release package for foobar2000 v2 x64.
- Updated component metadata and embedded project URLs to point to `Ci303/foobar2000-milkdrop2-fixes`.
- Documented current fixes, installation steps, runtime paths, and release usage in the README.
- Includes the recent maintenance fixes already merged into this repository:
  - settings UI cleanup and dark mode fixes
  - font dialog and album art fixes
  - keyboard shortcut fixes and help overlay alignment
  - song time display stabilisation
  - fullscreen, resize, and settings-apply stability fixes
  - remembered-preset startup handling improvements

## Current foobar2000 Component Status

In order to complete the port from Winamp some functionality has been removed, lost, or modified:

- Processes single-precision floating-point audio samples instead of 8-bit [modified].
- Title and custom messages and textures [modified, still use INI files].
- Customization saved in INI file [removed, moved to foobar2000 preferences].
- Older presets may not fully work [changed EEL and upgraded Shader Model to 4.0].
- Desktop mode [removed].
- Fake fullscreen mode and dual header functionality [removed].
- VJ mode [removed].

## MilkDrop 2 Library Notes

Prerequisites to build `vis_milk2.dll`:

- [Winamp 5.55 SDK](http://forums.winamp.com/showthread.php?t=252090): the files required to build the DLL are included in this repository.
- [NS-EEL2 Library](https://github.com/justinfrankel/WDL/tree/main/WDL/eel2) (included in [WDL](https://www.cockos.com/wdl/)): the files required to build the DLL are included in this repository.
- [DirectXTK Library](https://github.com/Microsoft/DirectXTK): the files required to build the DLL are imported via the NuGet package manager.

Breaking changes, relative to version 2.25c:

- The shader files in the `Winamp\Plugins\Milkdrop2\data` have been updated to match the DirectX 11 data layout.
- The [Legacy DirectX SDK](https://www.microsoft.com/en-us/download/details.aspx?id=6812) is no longer required to build due the update to DirectX 11 (see [Where is the DirectX SDK (2021 Edition)?](https://walbourn.github.io/where-is-the-directx-sdk-2021-edition/)).
- The minimum OS version supported is Windows 7. Consequently, the [DirectX End-User Runtime](https://www.microsoft.com/en-us/download/details.aspx?id=8109) installation is not required as it is included as a core component of the OS.
- Removed system callback API, which was mainly used to open websites in the internal browser.
- NS-EEL2 is _not_ built in EEL1 compatibility mode. So presets that use that syntax will not compile. Refer to the [WDL documentation](https://www.cockos.com/EEL2/) for syntax. Known EEL1 functions that have alternate EEL2 syntax or were deprecated are:
  `assign`, `if`, `equal`, `below`, `above`, `bnot`, `megabuf`, `gmegabuf`, `sigmoid`, `band`, `bor`
  <br />Example of preset change to work in EEL2:

```c
per_frame_22=vx2 = if(above(x2,0),vx2,abs(vx2)*0.5);
```

Should be:

```c
per_frame_22=vx2 = x2 > 0 ? vx2 : abs(vx2) * 0.5;
```

## MilkDrop 2 Release Notes

Open source release's `README.txt`:

```text
MilkDrop 2 development README

Author:       Ryan Geiss
Last updated: 18 May 2013

GETTING STARTED

To get started, either download the .zip file here (which contains a snapshot
of the MilkDrop 2 source code on 5/13/13, the day it was open-sourced), or
go to 'code' tab and execute the 'git clone' command given there to pull down
the source.

To build the Winamp / Windows version (which is the only build supported
so far), you'll need Visual Studio [Visual C++] 2008 or later.  (The free
'Express' editions of Visual Studio will work just fine, and they give you
all the functionality.)

Once it's installed, open Visual Studio and open the project
"src/vis_milk2/milkdrop_DX9.sln".  Then select either the Debug or Release
configuration, whichever you want.  Then build it.

If it gets through the compile and link but then gives an error when
trying to write the final binary (vis_milk2.dll) to disk, do the following:
In the Solution Explorer, right click on the "vis_milk2" project and click
Properties.  Then, under Configuration Properties, click on Linker.  To the
right, the first item you'll see is "Output File", and it will be set to
"$(ProgramFiles)\Winamp\plugins\vis_milk2.dll" (without the double quotes).
You might have to change this to write the file to some "normal" directory,
rather than the (proteted) Program Files directory.  Then, after building,
you'll want to manually copy vis_milk2.dll to the Winamp\Plugins directory
(and repeat this each time you build) (or maybe try starting Visual Studio
as an Administrator, so it can just write the file to where you want it,
in the first place).

Once the .DLL under Program Files is updated, you can start Winamp.  Hit
CTRL+K to select MilkDrop 2 as your visualizer; hit ALT+K to configure it
(optional); or play some music and then click the 'visualization' tab to
start it.  Double-click the visualization to go full-screen.

You can attach the Visual Studio debugger to MilkDrop while it is running,
as long as the DLL that's running matches the source code in Visual Studio.
From within Visual Studio, just go to the Debug menu and select 'Attach
to Process'.  Then find Winamp.exe and it should start.  You can then
see debug output in the visual studio window, set breakpoints, move the
instruction pointer around, look at variable values, and even
modify code once a breakpoint is hit -- the compiler will recompile it
on-the-fly and you'll just keep going.  (The Visual Studio debugger is
absolutely mind-blowingly awesome.)

You'll also need to have some preset files (*.milk) in
$(ProgramFiles)\Winamp\Plugins\Milkdrop2\presets, but if you installed Winamp,
then there will already be some there for you to play with.

To learn how to alter or author new MilkDrop presets, see
$(ProgramFiles)\Winamp\Plugins\Milkdrop2\docs\milkdrop_preset_authoring.html.
```
