# Musique EQ and Filter

Musique EQ and Filter is a Windows equaliser and high/low-pass filter for corrective, creative and mix-bus tone shaping. It is available as a Standalone application and a VST3 plug-in.

## Formats

- Windows x64 Standalone
- Windows x64 VST3

## Install a release

1. Download the Windows installer or portable ZIP from this repository's Releases page.
2. Run the installer, or extract the ZIP and copy the complete .vst3 bundle to a VST3 location scanned by your host.
3. Rescan plug-ins in the host, then insert the effect on the track or bus you want to process.

## Equalisation tools

The processor provides:

- Five gain bands: Low, Low Mid, Mid, High Mid and High.
- Independent frequency control for every band.
- Q control for the bell-band width.
- Switchable high-pass and low-pass filters.
- Adjustable cutoff frequency and filter slopes from 12 to 48 dB/octave.
- Mix, Output, Bypass and Mono controls for practical comparison.

For corrective work, enable a filter first when unwanted sub or high-frequency content is present, then make smaller band moves. Use Mix and Output for level-matched comparisons.

## Factory presets

The 24 factory presets cover master and mix-bus starts, corrective EQ, streaming checks, band-limited monitoring, vocal shaping, drum-bus work, low-end control, air lift and thin-source repair.

## Build from source

Requirements: Windows x64, PowerShell, Git, CMake 3.22 or later, Visual Studio 2022 (or Build Tools) with Desktop development with C++, and JUCE 8.0.4.

~~~powershell
.\_build_all.ps1 -Configuration Release -BootstrapJuce
~~~

To use an existing JUCE 8.0.4 checkout:

~~~powershell
.\_build_all.ps1 -Configuration Release -JuceDir C:\Dev\JUCE
~~~

The build produces Standalone and VST3 artefacts.

## Package a local build

~~~powershell
.\_package_release.ps1 -Configuration Release -BootstrapJuce
~~~

The script creates a portable Windows package and, when Inno Setup 6 is installed, a Windows installer. Use the SkipInstaller option when an installer is not required.

## Repository contents

| Path | Purpose |
| --- | --- |
| Source/ | Plug-in source, effect engines and visual assets |
| Presets/ | Factory preset bank |
| FXShared/ | Local shared UI and audio helpers required by this plug-in |
| installer/ | Windows installer definition |

## Licence and support

This project is source-available, not open source. See [LICENSE.md](LICENSE.md) for the permitted use of source and binaries. For a released-build issue, open an issue with the Windows version, host name/version, plug-in format and steps to reproduce it.
