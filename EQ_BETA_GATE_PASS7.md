# EQ Beta Gate - Passe 7

Date: 2026-05-17 19:12:49 +02:00  
Scope: Windows `MusiqueEQ` Standalone + VST3, Debug + Release.

## Decision

**NO-GO for beta release.**

Automated gate is green, but the required manual audio/UI QA has not been completed in this session. The release can move to **GO** after the manual checklist below is completed without critical findings.

## Automated Validation

| Command | Result |
| --- | --- |
| `cmake --build ..\build --config Debug --target MusiqueEQDSPTests` | PASS |
| `ctest --test-dir ..\build -C Debug -R MusiqueEQDSPTests --output-on-failure` | PASS |
| `cmake --build ..\build --config Release --target MusiqueEQDSPTests` | PASS |
| `ctest --test-dir ..\build -C Release -R MusiqueEQDSPTests --output-on-failure` | PASS |
| `cmake --build ..\build --config Debug --target MusiqueEQ_Standalone` | PASS |
| `cmake --build ..\build --config Debug --target MusiqueEQ_VST3` | PASS |
| `cmake --build ..\build --config Release --target MusiqueEQ_Standalone` | PASS |
| `cmake --build ..\build --config Release --target MusiqueEQ_VST3` | PASS |

Direct runner summaries:
- Debug: `checks=602 failures=0`
- Release: `checks=602 failures=0`

## Artifacts

| Artifact | Exists |
| --- | --- |
| `..\build\fx-eq\MusiqueEQ_artefacts\Debug\Standalone\Musique EQ & Filter.exe` | YES |
| `..\build\fx-eq\MusiqueEQ_artefacts\Debug\VST3\Musique EQ & Filter.vst3` | YES |
| `..\build\fx-eq\MusiqueEQ_artefacts\Release\Standalone\Musique EQ & Filter.exe` | YES |
| `..\build\fx-eq\MusiqueEQ_artefacts\Release\VST3\Musique EQ & Filter.vst3` | YES |

## Pluginval

Status: **SKIPPED - not found in PATH**  
Detection command: `Get-Command pluginval -ErrorAction SilentlyContinue`

This skip is non-blocking for this pass because `pluginval` is optional when unavailable, but a later installed `pluginval` run should validate both Debug and Release VST3 bundles before wider beta distribution.

## Warnings

Non-blocking warnings observed:
- `FXShared\FXLookAndFeel.h(124,99): warning C4100: 'slider' parameter unreferenced`
- JUCE Release build warning: `JUCE_DISPLAY_SPLASH_SCREEN is ignored`

No new blocking warning was identified during this pass.

## Manual QA Checklist

- [ ] Standalone Debug opens without crash.
- [ ] Standalone Release opens without crash.
- [ ] Audio in/out present, meters and clip LED active with real signal.
- [ ] Bypass is transparent by ear, including with mono/output/HPF/LPF modified.
- [ ] Mono/stereo works without UI desynchronization.
- [ ] Presets: initial `Manual State`, prev/next/select/save, factory beta presets, user preset reload.
- [ ] Mix `0/100`, output low/nominal/hot.
- [ ] Five EQ bands: drag frequency/gain, gain knobs, Q knob, Q extremes.
- [ ] HPF/LPF: select while off, enable, slope `12/24/48`, horizontal drag.
- [ ] SAFE/TRIM: stacked boosts trigger TRIM, neutral return goes back to SAFE.
- [ ] UI: no obvious overlap in graph, selected-band panel, badges, labels, or header.

## Known Beta Limits

- `A/B` is intentionally disabled in this pass; recall A/B remains out of scope.
- `pluginval` was not run because it is not installed or not in PATH.
- Manual audio/UI QA is still required before beta GO.
