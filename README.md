# MilkDrop 2 Visualization Component for foobar2000

Port of Winamp's MilkDrop 2 visualization library from its original DirectX 9 version to use DirectX 11.1.

## Maintenance Status

The original upstream project at `jecassis/foo_vis_milk2` is end-of-life.

This repository is an independent continuation focused on keeping the foobar2000 component usable on current systems, especially foobar2000 v2 and x64 installs. It keeps the upstream code and licensing intact, but documents and carries the practical fixes that were needed in day-to-day use.

This is not a new renderer or a redesign of MilkDrop. The aim is to keep the DirectX 11 foobar2000 port working reliably, make the preferences pages easier to use, and fix regressions that showed up in real use, whilst adding extra functionality.

Existing licensing and attribution are preserved under MPL-2.0.

## Photosensitivity Warning

MilkDrop presets can contain rapid flashing, strobing, high-contrast motion, intense colour changes, and fast full-screen movement. These visuals may trigger seizures, migraines, dizziness, nausea, or discomfort in people with photosensitive epilepsy or other visual sensitivities.

## What Has Been Fixed Here

This repository already includes the DirectX 11 foobar2000 port from upstream. On top of that, the recent maintenance work in this branch focuses on the following:

- Dark mode work for the foobar2000 preferences pages, including the font selection dialog.
- Preference page cleanup so controls fit properly, focus behaves more predictably, and broken or misleading UI elements have been corrected.
- Preference page layout updates for the new mouse control options so the right-hand settings column renders cleanly on current foobar2000 builds.
- Restored working title and time display shortcuts.
- Shortcut handling aligned with the on-screen help overlay so the documented keys match actual behaviour.
- Reworked the built-in help overlay so it lays out on-screen more cleanly and includes the newer mouse controls and clearer playback shortcut guidance.
- Refined the help overlay into a stable left-anchored two-column layout and fixed the embedded text loading so stray characters are not rendered.
- Stabilised the song time display so the elapsed and remaining time do not jump around as the timer updates.
- Fixed album art and font dialog issues in the preferences UI.
- Fixed album artwork toggle persistence so it now remembers the selected state across restarts.
- Improved custom message and sprite helper behaviour so the related settings buttons work with the expected `milk2_msg.ini` and `milk2_img.ini` files.
- Improved panel resize, fullscreen transitions, and settings-apply handling to avoid freezes and bad reinitialisation behaviour.
- Added safer startup preset handling so the component remembers the last working preset instead of repeatedly restoring a broken one.
- Fixed startup behaviour where some remembered presets could open in a degraded state because they required shader fallback on launch.
- Reworked the legacy wait-string editor path used by preset, wave and shape code editing to avoid unsafe buffer aliasing and cursor/render edge-case bugs.
- Improved long song title rendering so oversized titles scale down to fit and fall back to ellipsis clipping instead of disappearing.
- Added optional mouse wheel volume control and optional single-click play/pause, with a short `Playing` / `Paused` overlay that follows the current animated song-title font settings.
- Added preset blacklisting, including a `Never Show Again` runtime action, a blacklist manager dialog, and persistent blacklist storage in the `milkdrop2` profile folder.
- Refined the mouse focus/playback interaction so accidental first-click pauses are less likely when MilkDrop has just regained focus.
- Added crash diagnostics and minidump logging under `<foobar2000 profile folder>\milkdrop2\crashlogs` to help investigate intermittent runtime crashes.

## Current Behaviour and Limits

- Default UI only. Columns UI is not supported.
- foobar2000 preferences are used for most component settings.
- Presets, textures, custom messages and custom sprites still use the `milkdrop2` profile directory.
- Older presets can still fail or partially render if they depend on unsupported EEL1 syntax, old shader assumptions, or missing texture packs.
- This is currently tested mainly on foobar2000 v2 x64, although the project can still be built for other targets present in the solution.

## How To Install and Use It

### 1. Install the component

- The easiest install path is the GitHub Releases page for this repository.
- The first published release is an x64 package intended for foobar2000 v2 x64.
- Download [foobar2000](https://www.foobar2000.org/download) and install it.
- Import `foo_vis_milk2.fb2k-component` in **File > Preferences > Components > Install...**.
- Restart foobar2000 after the component is installed.

### 2. Add presets and textures

- Presets go in `<foobar2000 profile folder>\milkdrop2\presets`.
- Texture packs go in `<foobar2000 profile folder>\milkdrop2\textures`.
- If you use presets from large packs, install the matching texture pack as well. A lot of "blank", flat-colour, or obviously broken starts are simply missing textures.
- If you want to bootstrap the recommended packs automatically, run [tools/install-milkdrop-resources.ps1](tools/install-milkdrop-resources.ps1). By default it targets `%AppData%\foobar2000-v2` and installs the recommended preset and texture repositories into the correct `milkdrop2` folders.

Useful preset sources:

- [Cream of the Crop Pack](https://github.com/projectM-visualizer/presets-cream-of-the-crop)
- [Base Milkdrop Texture Pack](https://github.com/projectM-visualizer/presets-milkdrop-texture-pack)
- [Milkdrop 2 Presets](https://github.com/projectM-visualizer/presets-milkdrop-original)

Example:

```powershell
.\tools\install-milkdrop-resources.ps1
```

### 3. Open MilkDrop in foobar2000

- Add the MilkDrop visualisation element to a Default UI layout, or open it from the visualisations area if your layout already includes it.
- Open **Preferences > Visualisations > MilkDrop** to configure:
  - max frame rate
  - preset timing
  - hard cuts
  - title display
  - image cache
  - fonts
  - optional mouse wheel volume and single-click play/pause controls
  - whether F-key overlay states should be restored on startup
  - preset blacklist editing

For refresh rates above 60 FPS, use `Unlimited`. The other higher frame-rate settings are not currently working correctly in this fork and fixing them is outside the current scope of this project.

### 4. Optional custom files

- Custom messages file: `<foobar2000 profile folder>\milkdrop2\milk2_msg.ini`
- Custom sprites file: `<foobar2000 profile folder>\milkdrop2\milk2_img.ini`

If those files are blank or missing, normal playback still works. They are only used for the optional custom message and sprite features.

### 5. Useful runtime shortcuts

- `F1`: show help
- `F2`: song title
- `F3`: time display
- `F4`: preset name
- `F5`: frames per second
- `F6`: preset rating
- `Space` / `H`: soft cut or hard cut to the next preset
- `Backspace`: previous preset
- `Alt+Enter`: toggle fullscreen

The full shortcut list is also shown in the built-in help overlay.

### 6. Optional mouse controls

- Mouse wheel can adjust foobar2000 volume when **Enable mouse wheel volume control** is turned on in MilkDrop preferences.
- Single left click can toggle play/pause when **Enable single-click play/pause** is turned on.
- To avoid accidental pauses, the first click after focus is regained or after the context menu closes only arms MilkDrop and shows `Click Again for Play/Pause`.
- Double click still toggles fullscreen.
- When single-click play/pause is enabled, MilkDrop shows a short `Playing` or `Paused` overlay using the current animated song-title font settings.

### 7. Preset blacklist

- Right click the visualiser and choose `Never Show Again` to blacklist the current preset immediately.
- The current preset name at the top of the context menu can open the preset file location in Explorer.
- Blacklisted presets are skipped permanently until they are removed from the blacklist manager in preferences.
- The blacklist manager can open an existing blacklisted file in Explorer and can browse the preset folder to add new `.milk` files.
- The blacklist is stored at `<foobar2000 profile folder>\milkdrop2\preset-blacklist.txt`.

## Current Release State

The current public release line includes the following user-visible behaviour changes beyond the original DirectX 11 foobar2000 port:

- Safer preset, wave and shape code editing in the built-in text editor path.
- Persisted album artwork display state.
- Song titles that scale down to fit before falling back to ellipsis clipping.
- Optional mouse controls for volume and play/pause.
- Persistent preset blacklist management and direct preset file access from the context menu.
- Blacklist manager improvements including preset-folder browsing, safer preferences integration, and the startup/runtime overlay-state option.
- Stable built-in help overlay formatting with current keyboard and mouse shortcut information.
- Crash diagnostics/minidump logging for intermittent runtime crash investigation.
- Preferences dialog layout fixes for the newer settings.

## Releases

GitHub Releases are the preferred distribution point for this repository.

- Release assets are named to make the target architecture obvious.
- Current public releases are focused on x64 foobar2000 builds first.
- If you need a different target from the solution, build it locally from source using the instructions below.

## Building

Prerequisites to build the `foo_vis_milk2.dll` component for foobar2000:

- [foobar2000 SDK](https://www.foobar2000.org/SDK): download the latest version and uncompress the contents in the `external/` folder and apply the [patch](external/fb2ksdk.patch).
- [NS-EEL2](https://github.com/justinfrankel/WDL/tree/main/WDL/eel2) (included in [WDL](https://www.cockos.com/wdl/)): the files required to build the DLL are included in this repository.
- [projectM EEL](https://github.com/projectM-visualizer/projectm-eval): clone the repository into the `external/` folder, checkout the `HEAD` of the `master` branch and apply the [patch](external/pmeel.patch). _This is the default expression evaluation library._
- [DirectXTK](https://github.com/Microsoft/DirectXTK): the files required to build the DLL are fetched via the NuGet package manager.
- [Windows Template Library (WTL)](https://wtl.sourceforge.io/): the files required to build the DLL are fetched via the NuGet package manager.
- [Visual Studio 2022](https://visualstudio.microsoft.com/vs/): open the [`foo_vis_milk2`](foo_vis_milk2.sln) solution, set `foo_vis_milk2` as the Startup Project, install WTL and DirectXTK as NuGet packages, select a configuration, and build the solution.

> Import the Visual Studio [installation configuration](.vsconfig) file to install required components such as the [Windows SDK](https://developer.microsoft.com/en-us/windows/downloads/windows-sdk/), [Active Template Library (ATL)](https://learn.microsoft.com/en-us/cpp/atl/atl-com-desktop-components) and [NuGet Package Manager](https://www.nuget.org/).

Refer to the [build pipeline](.github/workflows/build.yml) jobs for a step-by-step guide on how to build. _Only x86 and x64 Intel architecture platforms are tested._

See [CHANGELOG](CHANGELOG.md) for additional details.

See [BUILDING](BUILDING.md) for solution build instructions.

See [TESTING](TESTING.md) for an outline of how to run a simple unit test and collect runtime coverage.

See [LICENSES](LICENSES.md) for third-party license details.

## Project Features

- Uses DirectX 11.1 (Direct3D 11.1, DXGI 1.6, Direct2D 1.1, DirectWrite 1.1) for rendering.
- Supports the Default User Interface (Default UI) only.
- Configurable through foobar2000 preferences instead of `.ini` files.
- Can build 32-bit and 64-bit x86 component configurations as well as ARM64 and ARM64EC.
- Built for foobar2000 2.0 and later with latest Windows 11 SDK (10.0.26100.0) and MSVC (v143).
- Updated all library dependencies to their latest available releases.
- Deprecated or insecure functions have been rewritten and most unused functionality removed.
- `vis_milk2` has been upgraded to use more modern C++ alongside the move to DirectX 11.
- Tested on foobar2000 v2.1.6 (x86 32-bit and x86 64-bit) and Microsoft Windows 11 (Build 26100.1742).
- Intel architecture versions support Windows 7 SP1 or later and ARM architecture versions support Windows 10 or later.
  - However, some features such as hybrid graphics, high DPI displays and HDR might not work if the DXGI version required to support them is not on the system.

## Repository Notes

The build assumes the following directory structure:

```text
 .github\ -> contains the continuous integration build pipeline.
 Bin\ -> generated by Visual Studio to contain the final DLLs and PDBs.
 component\ -> generated by the packaging script to layout the component prior to archiving and compression.
 data\ -> contains static files to be included in the component package.
     pdbs\ -> generated by the packaging script and contains the PDBs for each release.
     foo_vis_milk2-*.fb2k-component -> generated by the packaging script and are the packaged component releases.
 external\ -> contains external library dependencies.
     directxtk_desktop_2019.*\ -> from NuGet, contains the DirectX Tool Kit (DirectXTK) for x86, x64 and ARM64EC.
     directxtk_desktop_win10.*\ -> from NuGet, the DirectX Tool Kit (DirectXTK) for ARM64.
     eel2\ -> contains the Nullsoft Expression Evaluator Library (NS-EEL).
     foobar2000\ -> contains most of the foobar2000 SDK download.
         component_client\ -> from the foobar2000 SDK, generates the DLL entrypoint function for the component.
         helpers\ -> from the foobar2000 SDK, constains a library of various helper code for the component.
         sdk\ -> from the foobar2000 SDK, contains declarations of services and various service-specific helper code.
         shared\ -> from the foobar2000 SDK, contains the various utility code used by foobar2000 components.
     libppui\ -> from the foobar2000 SDK, contains a library of helper code, mainly Windows UI code.
     nu\ -> contains the Nullsoft utilities.
     pfc\ -> from the foobar2000 SDK, a class library used by the foobar2000 SDK.
     projectm-eval\ -> from GitHub, contains the projectM expression evaluation library.
     winamp\ -> contains header files, shader files and documentation from the Winamp release.
         data\ -> contains the Winamp pixel and vertex shaders.
         docs\ -> contains the MilkDrop 2 documentation.
     wtl.*\ -> from NuGet, the Windows Template Library (WTL).
 foo_vis_milk2\ -> contains the foobar2000 component code.
 Obj\ -> generated by Visual Studio to contain the intermediate build object files.
 tools\ -> contains the packaging and formatting scripts.
 vis_milk2\ -> contains the MilkDrop 2 visualization library code.
```

Additional repository notes:

- Main x64 build output: `Bin\x64\Release\foo_vis_milk2.dll`
- Typical foobar2000 x64 deployment path: `<foobar2000 profile folder>\user-components-x64\foo_vis_milk2\`
- Runtime presets, textures and INI files are read from: `<foobar2000 profile folder>\milkdrop2\`
- This repository is for the stable DirectX 11 foobar2000 component. Experimental DX12 work is intentionally out of scope here.
- Higher-than-60 FPS fixed-rate options in the preferences UI are not currently considered supported here; `Unlimited` is the intended setting for high-refresh displays.

### Build Notes

- Removed `/d2notypeopt` Visual C++ compiler option as it is applied by default on Visual Studio 2019 version 16.6 and later. ([1](https://hydrogenaud.io/index.php/topic,108411.0.html), [2](https://developercommunity.visualstudio.com/t/invalid-function-call-de-virtualization/1125222))
- Built all targets using v143 Platform Toolset for as `/arch` being "Not set" should default to `/arch:SSE2` on Visual Studio 2022 version 17.10 and later. ([1](https://hydrogenaud.io/index.php/topic,125795.0.html), [2](https://developercommunity.visualstudio.com/t/Cannot-disable-AVX-and-AVX2-in-VS-2022/10497078))

## MilkDrop 2

MilkDrop 2 (`vis_milk2`) is a music visualizer - a "plug-in" to the Winamp music player. The changes to the [MilkDrop 2 source code release](https://sourceforge.net/projects/milkdrop2/) from 5/13/13 (version 2.25c) include:

- Porting VMS from DirectX 9 to Direct X 11.1. DirectX 11.1 is Direct3D 11.1, DXGI 1.6, Direct2D 1.1, and DirectWrite 1.1.
- Porting text layout and rendering from D3DX9 and GDI to DirectWrite and Direct2D, respectively.
- Building DLL with Visual Studio 2022 (v143) Platform Toolset.
- Minor bug and typo fixing so that the plug-in can be used in Winamp and foobar2000 music players without crashing.
- Fixing of string resources to flow consistently with Segoe UI spacing and sizing.
- Minor cleaning and updating of configuration panel to match functioning features and UI modifications.
- Developer experience improvements, such as:
  - Updated dependencies to latest available versions.
  - Refactored EEL2 and DirectXTK into separate projects.
  - PCH and multiprocessor compile enabled for fast builds.
  - Buildable with C++20 compiler, including the address sanitizer and fuzzer. Builds clean using `/W4` (level 1,2,3,4 compiler warnings) and `/WX` (treat compiler warnings as errors) build options.
  - All character string and memory manipulation functions migrated to use their respective secure CRT versions.
  - Several utility functions and container classes replaced with their STL equivalents.
  - Enabled and added x86 and ARM 64-bit builds (x64, ARM64 and ARM64EC platforms) in addition to the upgraded x86 32-bit (Win32 platform) one. The 64-bit DLLs are not as extensively tested since most music players are still 32-bit applications.
  - Added a minimal unit test with associated infrastructure and some API service mock classes to test DLL initialization.
  - Added CI pipeline (GitHub Actions).
  - Enforced more consistent formatting (with ClangFormat), line endings, and file encodings. Important notes:
    - `*.rc` encoding was kept as "Western European (Windows) - Codepage 1252".
    - `*.vcxproj/*.sln` encoding was kept as "Unicode (UTF-8 with signature/BOM) - Codepage 65001".
    - All other text files use "Unicode (UTF-8 without signature) - Codepage 65001". All text files use CRLF line endings.
  - Updated comments and consolidated documentation.
