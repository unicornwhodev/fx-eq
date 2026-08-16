# Musique EQ and Filter

Free Windows x64 equalizer/filter effect from the Musique FX collection.

Musique EQ and Filter provides multi-band EQ and high/low-pass filtering in Standalone and VST3 formats.

## Download
Release assets are intended to include a Windows x64 installer and a portable package with Standalone + VST3 + factory presets.

## Build
```powershell
.\_build_all.ps1 -Configuration Release -BootstrapJuce
```
Or use `-JuceDir C:\Dev\JUCE` with JUCE 8.0.4.

## Package
```powershell
.\_package_release.ps1 -Configuration Release -BootstrapJuce
```

## Repository layout
- `Source/` — EQ/filter plugin source and UI assets
- `Presets/` — factory preset bank
- `FXShared/` — small shared runtime/UI dependency embedded for standalone builds
- `installer/` — release installer definition

Historical validation reports, manual QA checklists, golden tests and pluginval tooling are intentionally excluded from the public source tree.

The plugin is free to use; source is **source-available**, not open source. See [LICENSE.md](LICENSE.md).
