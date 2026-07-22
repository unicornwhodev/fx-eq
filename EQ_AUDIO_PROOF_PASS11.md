# EQ Audio Proof PASS11

Date: 2026-07-05

## Scope

PASS11 renforce la preuve audio automatique de `Musique EQ and Filter` sans changer le comportement sonore nominal attendu. La passe ajoute:

- mesure impulse/FFT via `juce::dsp::FFT` ordre 14, impulse stereo 16384 samples;
- test de latence nulle et position d'impulse;
- stabilite apres changements successifs de sample rate;
- golden audio JSON deterministe 48 kHz stereo;
- snapshots de coefficients EQ/HPF/LPF exposes uniquement sous `MUSIQUE_EQ_DSP_TESTS`;
- mesure explicite de mouvement de coefficients sous gain/Q extremes.

## Files And Artifacts

- Code test-only: `fx-eq/Source/PluginProcessor.h`, `fx-eq/Source/PluginProcessor.cpp`
- Tests: `fx-eq/Tests/EQProcessorTests.cpp`
- Golden reference: `fx-eq/Tests/golden/eq_reference_48k_stereo.json`
- Debug runner: `build-codex-fx-suite-ninja/fx-eq/MusiqueEQDSPTests_artefacts/Debug/MusiqueEQDSPTests.exe`
- Release runner: `build-codex-fx-suite-ninja-release/fx-eq/MusiqueEQDSPTests_artefacts/Release/MusiqueEQDSPTests.exe`
- Debug VST3: `build-codex-fx-suite-ninja/fx-eq/MusiqueEQ_artefacts/Debug/VST3/Musique EQ and Filter.vst3`
- Release VST3: `build-codex-fx-suite-ninja-release/fx-eq/MusiqueEQ_artefacts/Release/VST3/Musique EQ and Filter.vst3`
- Pluginval summary: `.tools/pluginval/logs/eq-pluginval-summary.json`

## Implemented Tests

- `testImpulseFftFrequencyResponse`
  - Neutral response flat at 60 Hz, 1 kHz, 12 kHz within +/-0.15 dB.
  - Bell +6 dB at 1200 Hz Q=1.2 reaches target range and leaves edges near unity.
  - Low shelf +6 dB at 100 Hz boosts 50 Hz and leaves 2 kHz near unity.
  - HPF 48 dB/oct at 300 Hz attenuates 60 Hz by at least 40 dB.
  - LPF 48 dB/oct at 3 kHz attenuates 12 kHz by at least 40 dB.
- `testLatencyAndImpulsePosition`
  - `getLatencySamples() == 0`.
  - Neutral and bypass impulse keep first significant sample at index 0.
  - EQ plus HPF/LPF creates no pre-delay.
- `testSampleRateChangeStability`
  - Same processor instance is prepared successively at 44.1 kHz, 96 kHz, 12 kHz, 192 kHz.
  - Aggressive EQ/HPF/LPF settings remain finite, bounded, and trim stays capped.
- `testExtremeCoefficientMovement`
  - Center-band coefficients remain finite.
  - Gain +24 dB, -24 dB, Q 0.3 and Q 8.0 produce measurable coefficient changes.
  - HPF/LPF 48 dB/oct expose four finite stages each.
- `testGoldenAudioReference`
  - Deterministic 4096-frame stereo render at 48 kHz, block size 256.
  - Taps compared with absolute tolerance `2e-4`.
  - RMS/peak compared with tolerance `1e-4`.
  - Trim compared with tolerance `0.05 dB`.
- Golden writer mode:
  - `MusiqueEQDSPTests --write-eq-golden <path>`
  - Normal test mode never regenerates the golden automatically.

## Validation Results

Builds:

```powershell
cmd.exe /d /s /c 'call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul && cmake --build build-codex-fx-suite-ninja --clean-first --target MusiqueEQDSPTests MusiqueEQ_Standalone MusiqueEQ_VST3'
```

Result: PASS.

```powershell
cmd.exe /d /s /c 'call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul && cmake --build build-codex-fx-suite-ninja-release --clean-first --target MusiqueEQDSPTests MusiqueEQ_Standalone MusiqueEQ_VST3'
```

Result: PASS.

CTest:

```powershell
ctest --test-dir .\build-codex-fx-suite-ninja -R MusiqueEQDSPTests --output-on-failure
ctest --test-dir .\build-codex-fx-suite-ninja-release -R MusiqueEQDSPTests --output-on-failure
```

Results:

- Debug: PASS, 1/1 test, total 2.36 s.
- Release: PASS, 1/1 test, total 0.80 s.

Direct runners:

```powershell
.\build-codex-fx-suite-ninja\fx-eq\MusiqueEQDSPTests_artefacts\Debug\MusiqueEQDSPTests.exe
.\build-codex-fx-suite-ninja-release\fx-eq\MusiqueEQDSPTests_artefacts\Release\MusiqueEQDSPTests.exe
```

Results:

- Debug: `[SUMMARY] checks=705 failures=0`
- Debug benchmark: `[BENCH] eq processBlock blocks=768 blockSize=256 ns/sample=196.823 peak=0.0267059`
- Release: `[SUMMARY] checks=705 failures=0`
- Release benchmark: `[BENCH] eq processBlock blocks=768 blockSize=256 ns/sample=19.0147 peak=0.0267059`

Pluginval:

```powershell
.\fx-eq\Tools\run-eq-pluginval.ps1 -DebugBuildRoot .\build-codex-fx-suite-ninja -ReleaseBuildRoot .\build-codex-fx-suite-ninja-release -StrictnessLevels 5,10
```

Results:

- Debug strictness 5: PASS, exit 0.
- Release strictness 5: PASS, exit 0.
- Debug strictness 10: PASS, exit 0.
- Release strictness 10: PASS, exit 0.

Hygiene:

```powershell
git diff --check
```

Result: PASS. Only existing CRLF conversion warnings were printed.

```powershell
Get-ChildItem -Path .\build-codex-fx-suite-ninja\fx-eq\MusiqueEQ_artefacts,.\build-codex-fx-suite-ninja-release\fx-eq\MusiqueEQ_artefacts -Recurse -Force | Where-Object { $_.Name -like '*Musique EQ & Filter*' -or $_.FullName -like '*Musique EQ & Filter*' } | Select-Object -ExpandProperty FullName
```

Result: PASS. No stale artifact path using `Musique EQ & Filter` was found.

## Remaining Limits

- CPU benchmark is informational and has no blocking threshold.
- Golden audio currently covers one deterministic stereo render at 48 kHz; sample-rate stability is covered separately by procedural buffers.
- Manual listening and UI QA were not executed in this PASS11 automation pass.
- Historical reports that mention the old `Musique EQ & Filter` name were not rewritten.

## Verdict

Automated audio proof PASS11: terminated and verified.
