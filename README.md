# MilkDrop 2 Visualization Component for foobar2000

Port of Winamp's MilkDrop 2 visualization library from its original DirectX 9 version to use DirectX 11.1.

## Maintenance Status

The original upstream project at `jecassis/foo_vis_milk2` is end-of-life.

This repository is an independent continuation focused on keeping the foobar2000 component usable on current systems, especially foobar2000 v2 and x64 installs. It preserves upstream licensing and attribution, but carries the fixes and usability work needed for day-to-day use.

This is not a new renderer or a redesign of MilkDrop. The goal is to keep the DirectX 11 foobar2000 port stable, usable, and maintainable.

## Photosensitivity Warning

MilkDrop presets can contain rapid flashing, strobing, high-contrast motion, intense colour changes, and fast full-screen movement. These visuals may trigger seizures, migraines, dizziness, nausea, or discomfort in people with photosensitive epilepsy or other visual sensitivities.

## Current Status

This branch already includes the upstream DirectX 11 foobar2000 port. Recent maintenance work is focused on four areas.

### UI and Preferences

- Dark mode support for the foobar2000 preferences pages, including the font selection dialog.
- Preference layout cleanup so newer controls fit properly and the right-hand settings column renders cleanly on current foobar2000 builds.
- Fixed album art and font dialog issues in the preferences UI.
- Embedded-panel title formatting now honours the configured title format and refreshes cleanly after preference edits.
- `Format Info...` is now a dark-mode-aware builder for title and artwork format fields, with examples, common metadata fields, and direct insertion back into the main preferences fields.

### Playback and Overlay Behaviour

- Restored working title and time display shortcuts.
- Help overlay updated and aligned with the actual keyboard and mouse behaviour.
- `F3` time display now shows `m:ss` or `h:mm:ss` without fractional milliseconds.
- Long song titles scale down to fit before falling back to ellipsis clipping.
- Center title animations no longer fight with `Playing` / `Paused` overlays or automatic title updates.
- Optional mouse wheel volume control and optional single-click play/pause have been added.

### Stability and Preset Handling

- Safer startup preset handling so the component remembers the last working preset instead of repeatedly restoring a broken one.
- Improved startup handling for presets that require shader fallback.
- Safer text-editor handling for preset, wave, and shape code editing.
- Render-lock hardening around album-art updates, blacklist preset replacement, and play/pause overlay launching.
- Persistent preset blacklist support, including `Never Show Again`, blacklist manager editing, and import/export.

### Diagnostics and Runtime Noise

- Crash diagnostics and minidump logging under `<foobar2000 profile folder>\milkdrop2\crashlogs`.
- Reduced release-build foobar2000 console noise from routine MilkDrop status and warning overlays.

## Current Behaviour and Limits

- Default UI only. Columns UI is not supported.
- foobar2000 preferences are used for most component settings.
- Presets, textures, custom messages, and custom sprites still use the `milkdrop2` profile directory.
- Older presets can still fail, crash, hang, or remain black if they depend on unsupported EEL1 syntax, old shader assumptions, missing texture packs, or other compatibility issues.
- This project is currently tested mainly on foobar2000 v2 x64, although the solution can still be built for other targets.
- FPS caps are selected from standard monitor rates: 30, 60, 75, 120, 144, and 240 FPS.
- The foobar2000 wrapper owns frame pacing in embedded and fullscreen modes so both paths honour the same preference without double-throttling.
- Direct3D presentation no longer waits on DXGI vsync; use the FPS preference to choose the intended cap.

## Install and Use

### 1. Install

- The easiest install path is the GitHub Releases page for this repository.
- Current public packages are x64 builds intended for foobar2000 v2 x64.
- Download [foobar2000](https://www.foobar2000.org/download) and install it.
- Close foobar2000, then run the release installer named like `foo_vis_milk2-0.2.1.27-installer.exe`. It installs the x64 component, shader data, recommended preset packs, textures, repaired preset additions, and an optional starter layout template into the foobar2000 v2 profile.
- If foobar2000 x64 is not installed, the installer explains that foobar2000 is required, opens the official foobar2000 download page, and exits. Install foobar2000 v2 x64 first, close it, then run the MilkDrop installer again.
- Restart foobar2000 after the component is installed.

### 2. Optional starter template

The installer copies a starter foobar2000 Default UI theme to:

`<foobar2000 profile folder>\milkdrop2\templates\foobar2000-milkdrop-starter.fth`

This template is intended to help non-technical users get a working MilkDrop layout without building one manually. It is copied as a template only; the installer does not overwrite the user's active foobar2000 layout.

To use it, open foobar2000 after installation and import the `.fth` theme from the profile folder path above.

The same template is stored in this repository at [templates/foobar2000-milkdrop-starter.fth](templates/foobar2000-milkdrop-starter.fth).

### 3. Optional manual resources

- Presets go in `<foobar2000 profile folder>\milkdrop2\presets`.
- Texture packs go in `<foobar2000 profile folder>\milkdrop2\textures`.
- The release installer already installs the recommended packs and repaired preset additions.
- If you install `foo_vis_milk2.fb2k-component` manually, run [tools/install-milkdrop-resources.ps1](tools/install-milkdrop-resources.ps1) to install the same vendored presets, textures, and repaired additions.
- Many "blank" or obviously broken presets are caused by missing textures.

Useful preset sources:

- [Cream of the Crop Pack](https://github.com/projectM-visualizer/presets-cream-of-the-crop)
- [Base Milkdrop Texture Pack](https://github.com/projectM-visualizer/presets-milkdrop-texture-pack)
- [Milkdrop 2 Presets](https://github.com/projectM-visualizer/presets-milkdrop-original)
- [Vendored resource archive snapshots](third_party/milkdrop-resources)
- [Fixed blacklist preset overrides](presets/fixed-blacklisted)

Manual resource install example:

```powershell
.\tools\install-milkdrop-resources.ps1
```

### 4. Configure

- Add the MilkDrop visualisation element to a Default UI layout, or open it from the visualisations area if your layout already includes it.
- Open **Preferences > Visualisations > MilkDrop** to configure:
  - preset timing and hard cuts
  - title display
  - album artwork display
  - image cache
  - fonts
  - optional mouse controls
  - preset blacklist editing

Title display format uses normal foobar2000 title formatting. Artwork display format can be left blank to use standard foobar2000 album art, or set to a full image path or a title-formatting script that returns one. The preferences page includes a `Format Info...` builder with examples, common fields, and direct insertion into either format box.

The max frame rate preference uses standard monitor rates: 30, 60, 75, 120, 144, and 240 FPS. The old `Unlimited` option is intentionally not exposed because it can overdrive the render loop and make foobar2000 less responsive.

FPS behaviour is host-paced:

- Embedded panel and fullscreen rendering both use the configured MilkDrop preference.
- MilkDrop's legacy internal sleep limiter is disabled while hosted by foobar2000 to avoid double-limiting.
- DXGI presentation is non-vsync-blocking, with optional page tearing controlled by the page-tearing preference.

### 5. Optional custom files

- Custom messages file: `<foobar2000 profile folder>\milkdrop2\milk2_msg.ini`
- Custom sprites file: `<foobar2000 profile folder>\milkdrop2\milk2_img.ini`

If those files are blank or missing, normal playback still works. They are only used for the optional custom message and sprite features.

### 6. Useful runtime shortcuts

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

### 7. Optional mouse controls

- Mouse wheel can adjust foobar2000 volume when **Enable mouse wheel volume control** is turned on in MilkDrop preferences.
- Single left click can toggle play/pause when **Enable single-click play/pause** is turned on.
- Double click still toggles fullscreen.
- When single-click play/pause is enabled, MilkDrop shows a short `Paused` overlay using the current animated song-title font settings.

### 8. Preset blacklist

- Right click the visualiser and choose `Never Show Again` to blacklist the current preset immediately.
- The current preset name at the top of the context menu can open the preset file location in Explorer.
- Blacklisted presets are skipped until they are removed from the blacklist manager in preferences.
- The blacklist manager can open an existing blacklisted file in Explorer and can browse the preset folder to add new `.milk` files.
- The blacklist manager supports Shift-click range selection and Ctrl-click individual selection when removing multiple entries.
- The blacklist is stored at `<foobar2000 profile folder>\milkdrop2\preset-blacklist.txt`.

## Releases

GitHub Releases are the preferred distribution point for this repository.

- Release assets are named to make the target architecture obvious.
- Current public releases are focused on x64 foobar2000 builds first.
- See [CHANGELOG](CHANGELOG.md) for per-release details.

## Privacy

foo_vis_milk2 does not collect, transmit, sell, or store personal user data.

The component runs locally inside foobar2000. It reads local foobar2000 playback state, visualisation audio data, album artwork provided by foobar2000, and files under the local `milkdrop2` profile folder for presets, textures, custom messages, custom sprites, crash logs, and configuration.

The installer runs locally and installs the component, shader data, presets, textures, and repaired preset additions into the foobar2000 profile. If foobar2000 x64 is not installed, it opens the official foobar2000 download page in the user's browser.

## Building

Prerequisites to build the `foo_vis_milk2.dll` component for foobar2000:

- [foobar2000 SDK](https://www.foobar2000.org/SDK): download the latest version, uncompress it into `external/`, and apply the [patch](external/fb2ksdk.patch).
- [NS-EEL2](https://github.com/justinfrankel/WDL/tree/main/WDL/eel2): included in this repository.
- [projectM EEL](https://github.com/projectM-visualizer/projectm-eval): clone into `external/`, checkout `master`, and apply the [patch](external/pmeel.patch).
- [DirectXTK](https://github.com/Microsoft/DirectXTK): fetched via NuGet.
- [Windows Template Library (WTL)](https://wtl.sourceforge.io/): fetched via NuGet.
- [Visual Studio 2022](https://visualstudio.microsoft.com/vs/): open [`foo_vis_milk2.sln`](foo_vis_milk2.sln), set `foo_vis_milk2` as the Startup Project, install the NuGet packages, select a configuration, and build.

> Import the Visual Studio [installation configuration](.vsconfig) file to install required components such as the Windows SDK, ATL, and NuGet Package Manager.

Additional references:

- [BUILDING](BUILDING.md)
- [TESTING](TESTING.md)
- [CHANGELOG](CHANGELOG.md)
- [LICENSES](LICENSES.md)
- [.github/workflows/build.yml](.github/workflows/build.yml)

## Developer Notes

- Main x64 build output: `Bin\x64\Release\foo_vis_milk2.dll`
- Typical foobar2000 x64 deployment path: `<foobar2000 profile folder>\user-components-x64\foo_vis_milk2\`
- Runtime presets, textures, and INI files are read from: `<foobar2000 profile folder>\milkdrop2\`
- The solution can still build x86, x64, ARM64, and ARM64EC, but public releases are currently focused on x64 first.
- The project uses DirectX 11.1 for rendering and targets foobar2000 2.0 and later.
- This repository is for the stable DirectX 11 foobar2000 component. Experimental DX12 work is intentionally out of scope here.

## Upstream Background

MilkDrop 2 (`vis_milk2`) is a music visualiser originally written for Winamp. Relative to the original source release, the upstream foobar2000 DirectX 11 port already did the heavy lifting:

- ported the renderer from DirectX 9 to DirectX 11.1
- moved text layout and rendering to DirectWrite and Direct2D
- modernised the project for current Visual Studio toolchains
- cleaned up a large amount of legacy code and dependency handling

This repository keeps that port working in current foobar2000 installs and focuses on maintenance, fixes, usability work, and release packaging rather than redesigning the visualiser itself.
