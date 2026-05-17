# EQ Pluginval - Passe 8

Date: 2026-05-17 19:22:28 +02:00  
Scope: Windows `MusiqueEQ` VST3 Debug + Release.

## Decision

**PASS pluginval.**

Both Debug and Release VST3 bundles pass the blocking `pluginval` strictness 5 gate. Optional strictness 10 also passes for both bundles.

This does not replace the manual audio/UI QA still listed in `EQ_BETA_GATE_PASS7.md`.

## Tool

- Executable: `D:\tmp\pluginval\expanded\pluginval.exe`
- Product version: `1.0.4`
- Source: Tracktion/pluginval GitHub Releases
- Local cache: `D:\tmp\pluginval`
- Logs: `D:\tmp\pluginval\logs`

No `pluginval` binary is stored in the repository.

## Builds

| Command | Result |
| --- | --- |
| `cmake --build ..\build --config Debug --target MusiqueEQ_VST3` | PASS |
| `cmake --build ..\build --config Release --target MusiqueEQ_VST3` | PASS |

## Pluginval Results

| Config | Strictness | Exit Code | Result | Log |
| --- | ---: | ---: | --- | --- |
| Debug | 5 | 0 | PASS | `D:\tmp\pluginval\logs\pluginval-debug-strictness-5.log` |
| Release | 5 | 0 | PASS | `D:\tmp\pluginval\logs\pluginval-release-strictness-5.log` |
| Debug | 10 | 0 | PASS | `D:\tmp\pluginval\logs\pluginval-debug-strictness-10.log` |
| Release | 10 | 0 | PASS | `D:\tmp\pluginval\logs\pluginval-release-strictness-10.log` |

Commands executed by `Tools\run-eq-pluginval.ps1`:

```powershell
"D:\tmp\pluginval\expanded\pluginval.exe" --strictness-level 5 "D:\Dev\Projects\musique\FX\build\fx-eq\MusiqueEQ_artefacts\Debug\VST3\Musique EQ & Filter.vst3"
"D:\tmp\pluginval\expanded\pluginval.exe" --strictness-level 5 "D:\Dev\Projects\musique\FX\build\fx-eq\MusiqueEQ_artefacts\Release\VST3\Musique EQ & Filter.vst3"
"D:\tmp\pluginval\expanded\pluginval.exe" --strictness-level 10 "D:\Dev\Projects\musique\FX\build\fx-eq\MusiqueEQ_artefacts\Debug\VST3\Musique EQ & Filter.vst3"
"D:\tmp\pluginval\expanded\pluginval.exe" --strictness-level 10 "D:\Dev\Projects\musique\FX\build\fx-eq\MusiqueEQ_artefacts\Release\VST3\Musique EQ & Filter.vst3"
```

## DSP Gate Recheck

| Command | Result |
| --- | --- |
| `ctest --test-dir ..\build -C Debug -R MusiqueEQDSPTests --output-on-failure` | PASS |
| `ctest --test-dir ..\build -C Release -R MusiqueEQDSPTests --output-on-failure` | PASS |

## Remaining Beta Blocker

`pluginval` is no longer a blocker. The remaining blocker from Passe 7 is manual audio/UI QA:
- standalone launch and close in Debug/Release;
- real audio in/out, meters, clip LED;
- bypass, mono/stereo, presets, drag handles, HPF/LPF, SAFE/TRIM;
- visual overlap and stale UI state checks.
