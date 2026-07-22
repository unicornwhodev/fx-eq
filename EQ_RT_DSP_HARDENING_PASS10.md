# EQ RT DSP Hardening - PASS10

Date: 2026-07-05 23:00 +02:00  
Scope: `fx-eq`, product actuel `Musique EQ and Filter`, durcissement temps reel DSP.

## Decision

**TECHNICAL PASS.**

Le chemin DSP EQ a ete durci sans changement intentionnel du son nominal:
- coefficients EQ/HPF/LPF generes via `IIR::ArrayCoefficients` et assignes en place;
- HPF/LPF 12/24/48 dB/oct remplaces par cascades Butterworth fixes sans `FilterDesign`;
- frequences des 5 bandes + HPF/LPF smoothees en multiplicatif/logarithmique;
- SAFE/TRIM cache sur gains, frequences, Q et sample rate, avec trim force a 0 dB en bypass ou `mix=0`;
- test transitoire automation ajoute;
- benchmark CPU informatif ajoute et imprime `[BENCH] ... ns/sample`.

## Code Changes Covered

- `fx-eq/Source/PluginProcessor.h`
  - Ajout des smoothers multiplicatifs de frequence.
  - Ajout du cache `TrimCache`.
  - Separation des changements de cibles EQ vs cut filters.
- `fx-eq/Source/PluginProcessor.cpp`
  - Remplacement des allocations `IIR::Coefficients::make*` par `IIR::ArrayCoefficients::make*`.
  - Cascades Butterworth fixes pour HPF/LPF: 12 dB = 1 biquad, 24 dB = 2 biquads, 48 dB = 4 biquads.
  - Reset HPF/LPF limite aux changements d'activation ou de pente.
  - Recalcul coefficients par chunks de 16 samples seulement quand smoothing/changement pertinent.
  - `computeInternalTrimDb` ne depend plus d'allocations de coefficients JUCE.
- `fx-eq/Tests/EQProcessorTests.cpp`
  - Test `testAutomationTransient`: changements brusques gain/frequence/Q/HPF/LPF sur signal continu.
  - Benchmark `runCpuBenchmark`: ligne `[BENCH] eq processBlock ... ns/sample=...`.
- `fx-eq/Tools/run-eq-pluginval.ps1`
  - Quand pluginval est resolu via `.tools\pluginval\bin`, ce dossier est prefixed au `PATH` du processus PowerShell courant avant execution.

## Build

| Command | Result |
| --- | --- |
| `cmd.exe /d /s /c 'call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul && cmake --build build-codex-fx-suite-ninja --clean-first --target MusiqueEQDSPTests MusiqueEQ_Standalone MusiqueEQ_VST3'` | PASS |
| `cmd.exe /d /s /c 'call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul && cmake --build build-codex-fx-suite-ninja-release --clean-first --target MusiqueEQDSPTests MusiqueEQ_Standalone MusiqueEQ_VST3'` | PASS |

## DSP Tests

| Command | Result |
| --- | --- |
| `ctest --test-dir .\build-codex-fx-suite-ninja -R MusiqueEQDSPTests --output-on-failure` | PASS, 1/1 |
| `ctest --test-dir .\build-codex-fx-suite-ninja-release -R MusiqueEQDSPTests --output-on-failure` | PASS, 1/1 |
| `& .\build-codex-fx-suite-ninja\fx-eq\MusiqueEQDSPTests_artefacts\Debug\MusiqueEQDSPTests.exe \| Select-String -Pattern '\[BENCH\]\|\[SUMMARY\]\|\[FAIL\]'` | PASS, `[SUMMARY] checks=623 failures=0`; `[BENCH] ... ns/sample=202.994 peak=0.0267059` |
| `& .\build-codex-fx-suite-ninja-release\fx-eq\MusiqueEQDSPTests_artefacts\Release\MusiqueEQDSPTests.exe \| Select-String -Pattern '\[BENCH\]\|\[SUMMARY\]\|\[FAIL\]'` | PASS, `[SUMMARY] checks=623 failures=0`; `[BENCH] ... ns/sample=18.6839 peak=0.0267059` |

## Pluginval

| Command | Result |
| --- | --- |
| `.\fx-eq\Tools\run-eq-pluginval.ps1 -DebugBuildRoot .\build-codex-fx-suite-ninja -ReleaseBuildRoot .\build-codex-fx-suite-ninja-release -StrictnessLevels 5,10` | PASS |

Details:
- `Debug strictness 5`: PASS, exit 0
- `Release strictness 5`: PASS, exit 0
- `Debug strictness 10`: PASS, exit 0
- `Release strictness 10`: PASS, exit 0
- Summary JSON: `.tools\pluginval\logs\eq-pluginval-summary.json`
- Pluginval path used: `.tools\pluginval\bin\pluginval.exe`

## Static Hygiene

| Command | Result |
| --- | --- |
| `git diff --check` | PASS, no whitespace errors; CRLF warnings only |
| `Get-ChildItem -Path .\build-codex-fx-suite-ninja,.\build-codex-fx-suite-ninja-release -Recurse -Filter '*Musique EQ & Filter*'` | PASS, no output |
| `rg -n "Musique EQ & Filter" build-codex-fx-suite-ninja\fx-eq\MusiqueEQ_artefacts build-codex-fx-suite-ninja-release\fx-eq\MusiqueEQ_artefacts -g "*"` | PASS, no match |

## Notes And Limits

- Le benchmark CPU est informatif et non bloquant, conformement au plan.
- Aucune QA audio/UI manuelle avec standalone n'a ete executee dans PASS10; cette validation reste rattachee au gate beta manuel de PASS9.
- Les changements `fx-delay` et `research/` preexistants restent hors perimetre de cette passe.
