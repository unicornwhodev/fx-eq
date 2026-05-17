# EQ Baseline Pass 0

Date: 2026-05-17  
Scope: Windows Debug baseline for `MusiqueEQ_Standalone` and `MusiqueEQ_VST3`.

## Build

Commands run from `D:\Dev\Projects\musique\FX\fx-eq`:

```powershell
cmake --build ..\build --config Debug --target MusiqueEQ_Standalone
cmake --build ..\build --config Debug --target MusiqueEQ_VST3
```

Result: pass.

Observed outputs:

- Standalone: `..\build\fx-eq\MusiqueEQ_artefacts\Debug\Standalone\Musique EQ & Filter.exe`
- VST3 bundle: `..\build\fx-eq\MusiqueEQ_artefacts\Debug\VST3\Musique EQ & Filter.vst3`
- VST3 binary: `..\build\fx-eq\MusiqueEQ_artefacts\Debug\VST3\Musique EQ & Filter.vst3\Contents\x86_64-win\Musique EQ & Filter.vst3`
- VST3 metadata: `..\build\fx-eq\MusiqueEQ_artefacts\Debug\VST3\Musique EQ & Filter.vst3\Contents\Resources\moduleinfo.json`

`moduleinfo.json` reports:

- Name: `Musique EQ & Filter`
- Version: `0.1.0`
- Vendor: `Musique`
- VST SDK: `VST 3.7.12`
- Category: `Fx`

Note: the build succeeded and regenerated VST3 metadata. Some binary timestamps predate this run because no source/object relink was required.

## Lancement

Automated standalone launch check:

- Launched `Musique EQ & Filter.exe` twice with `Start-Process`.
- The process was still alive after 5-8 seconds on both attempts.
- No immediate crash was observed.
- `CloseMainWindow()` was sent successfully, but the process did not exit within 10 seconds in this agent-run context and was force-terminated.

Finding:

- `PASS0-001`: standalone launch is stable enough for a smoke start, but controlled close did not complete within 10 seconds from automation. Needs manual confirmation by closing the visible window directly.

## Audio

Not fully validated in this automated session:

- Real audio input/output monitoring.
- Auditory bypass comparison.
- Pop/click behavior during repeated bypass toggles.
- Mono/stereo listening check.
- Output sweep by ear.

Static baseline observations from `Source\PluginProcessor.cpp`:

- `mono` is applied before the bypass branch.
- When `bypass` is enabled, the processor still applies `output` gain before returning.

Finding:

- `PASS0-002`: bypass is not a fully transparent dry pass in the current implementation. With bypass enabled, `mono` and `output` can still affect the signal.

## Presets

Static baseline observations from `Source\PluginEditor.cpp`:

- Factory/user presets are loaded through `fx::preset::loadAllPresets("fx-eq")`.
- If presets exist, the editor selects index 0 and immediately applies it to the APVTS when the editor is constructed.
- Previous and next buttons navigate the combo box.
- Save writes the current APVTS values to a user preset with a generated `User_HHMMSS` name.
- The A/B button is visible but has no assigned behavior in the current editor code.

Findings:

- `PASS0-003`: opening the editor can apply the first preset automatically, which may overwrite a host-restored state when the UI is created.
- `PASS0-004`: the visible `A/B` control appears non-functional in this baseline.

Manual checks still required:

- Select every factory preset and confirm UI/audio state.
- Save a user preset and confirm it appears after reload.
- Confirm no preset selection/UI desync after next/previous navigation.

## Contrôles

Current exposed controls:

- Bands: `Low`, `Low Mid`, `Mid`, `High Mid`, `High`
- Shared `Q`
- `Mix`
- `Output`
- `Bypass`
- `Mono`
- Header `SAFE`/`TRIM`
- Disabled settings button
- Visible but currently unimplemented `A/B`

Manual checks still required:

- Sweep all five gain bands.
- Sweep `Q` from minimum to maximum.
- Test `Mix` at `0%` and `100%`.
- Test `Output` low, nominal and hot.
- Toggle `Mono` while passing stereo material.

## Visualisation

Static baseline observations:

- Meters use `fx::AudioVisualState` input/output snapshots.
- Clip LED turns on when max output meter level is above `0.98`.
- `SAFE` changes to `TRIM -XdB` when `currentInternalTrimDb > 0.25`.
- Header mono label changes between `MONO IN` and `STEREO IN`.
- EQ curve is drawn from current APVTS gain and Q values, not measured audio.

Manual checks still required:

- Confirm meters react only to real signal.
- Confirm clip LED follows actual output overload risk.
- Confirm SAFE/TRIM appears with stacked boosts and returns to SAFE at neutral settings.
- Confirm no graph/header overlap or stale labels after extreme sweeps.

## Bugs Observés

- `PASS0-001`: standalone controlled close did not exit within 10 seconds under automation; manual close needs confirmation.
- `PASS0-002`: bypass is not fully transparent because mono/output processing still occurs while bypassed.
- `PASS0-003`: editor construction auto-applies the first preset when presets exist, risking host state overwrite.
- `PASS0-004`: A/B button is visible but not implemented.
- `PASS0-005`: full audio/UI smoke remains incomplete because this agent session cannot perform listening or direct GUI interaction.

## Décision Go/No-Go

Decision: conditional go for Passe 1 planning/work, no-go for claiming a complete manual baseline.

Rationale:

- Build baseline is clear: both Debug targets compile.
- Launch baseline is partially clear: no immediate crash, but automated close is questionable.
- Several current-state issues are documented before refactor.
- Manual audio and visual QA must still be completed by a human before treating this baseline as complete.
