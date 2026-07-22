# EQ Beta Gate - Passe 9

Date: 2026-07-05 22:40 +02:00  
Scope: `fx-eq`, product actuel `Musique EQ and Filter`, Windows Ninja single-config Debug + Release.

## Decision

**TECHNICAL PASS. MANUAL QA PENDING.**

Le gate technique beta est reproductible sur le nom actuel `Musique EQ and Filter`:
- build Debug et Release passes;
- tests DSP Debug et Release passent avec `checks=619 failures=0`;
- `pluginval` strictness 5 et 10 passe en Debug et Release;
- l'installer EQ Release est genere et inclut VST3, standalone et `fx-eq\Presets`;
- aucun artefact genere `Musique EQ & Filter` n'est reste sous les build roots Ninja.

Le gate beta complet ne doit pas etre marque ferme tant que la QA audio/UI manuelle avec audio reel n'a pas ete executee.

## Changes Covered

- `fx-eq/CMakeLists.txt`
  - `Presets/factory_bank.json` est embarque dans `MusiqueEQAssets`.
  - `MusiqueEQDSPTests` lie aussi `MusiqueEQAssets` pour verifier le JSON embarque.
- `fx-eq/Source/PluginEditor.cpp`
  - Les presets factory EQ sont charges depuis `BinaryData::factory_bank_json`.
  - Les presets utilisateur sont ajoutes via `FXShared`.
- `fx-eq/Tests/EQProcessorTests.cpp`
  - Nouveau test de non-regression: parse du `factory_bank.json` embarque et verification des 8 presets beta.
- `FXShared/FXComponents.h`
  - Ajout du fallback user data `%APPDATA%\Musique\FX\<fxName>\Presets\User` quand le dossier repo du plugin n'est pas disponible.
  - Chargement explicite des presets utilisateur via `loadUserPresets`.
- `fx-eq/Tools/run-eq-pluginval.ps1`
  - Cible `Musique EQ and Filter.vst3`.
  - Parametres `-DebugBuildRoot`, `-ReleaseBuildRoot`, `-StrictnessLevels`.
  - Recherche `pluginval` dans le `PATH`, puis `.tools\pluginval\bin`.
  - Logs dans `.tools\pluginval\logs`.
- `installer/build-installers.ps1`
  - Parametres `-PluginId` et `-Config Debug|Release`.
  - Chemins d'artefacts alignes sur la config demandee.
  - Copie optionnelle du dossier `Presets` vers `{app}\FX\<pluginFolder>\Presets`.

## Artifact Hygiene

Commande:

```powershell
Get-ChildItem -Path .\build-codex-fx-suite-ninja,.\build-codex-fx-suite-ninja-release -Recurse -Force -Filter '*Musique EQ & Filter*' | Select-Object -ExpandProperty FullName
```

Resultat: **PASS**, aucune sortie.  
Les anciens rapports historiques `PASS0`, `PASS7`, `PASS8` n'ont pas ete reecrits.

## Ignored Generated Outputs

| Command | Result |
| --- | --- |
| `git check-ignore -v .tools\pluginval\logs\eq-pluginval-summary.json .tools\pluginval\logs\pluginval-debug-strictness-5.log` | PASS, `.tools/` ignore |
| `git check-ignore -v -- 'build-codex-fx-suite-ninja\fx-eq\MusiqueEQ_artefacts\Debug\VST3\Musique EQ and Filter.vst3' 'build-codex-fx-suite-ninja-release\installer\output\Musique-EQ-and-Filter-Setup.exe'` | PASS, `**/build-*/` ignore |

## Build

| Command | Result |
| --- | --- |
| `cmake -S . -B build-codex-fx-suite-ninja -G Ninja -DCMAKE_BUILD_TYPE=Debug -DFETCHCONTENT_SOURCE_DIR_JUCE=D:/Dev/Projects/musique/FX/build-codex-fx-suite-ninja/_deps/juce-src` | PASS |
| `cmd.exe /d /s /c '"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" && cmake -S . -B build-codex-fx-suite-ninja-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DFETCHCONTENT_SOURCE_DIR_JUCE=D:/Dev/Projects/musique/FX/build-codex-fx-suite-ninja/_deps/juce-src'` | PASS |
| `cmd.exe /d /s /c '"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" && cmake --build build-codex-fx-suite-ninja --clean-first --target MusiqueEQDSPTests MusiqueEQ_Standalone MusiqueEQ_VST3'` | PASS |
| `cmd.exe /d /s /c '"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" && cmake --build build-codex-fx-suite-ninja-release --clean-first --target MusiqueEQDSPTests MusiqueEQ_Standalone MusiqueEQ_VST3'` | PASS |

Notes:
- Les commandes de build finales ont relance CMake apres l'ajout du test `BinaryData`.
- Warnings observes: warnings CMake/JUCE dev `CMP0175` deja presents, non bloquants pour ce gate.

## DSP Tests

| Command | Result |
| --- | --- |
| `ctest --test-dir .\build-codex-fx-suite-ninja -R MusiqueEQDSPTests --output-on-failure` | PASS, 1/1 |
| `ctest --test-dir .\build-codex-fx-suite-ninja-release -R MusiqueEQDSPTests --output-on-failure` | PASS, 1/1 |
| `& .\build-codex-fx-suite-ninja\fx-eq\MusiqueEQDSPTests_artefacts\Debug\MusiqueEQDSPTests.exe \| Select-Object -Last 8` | PASS, `[SUMMARY] checks=619 failures=0` |
| `& .\build-codex-fx-suite-ninja-release\fx-eq\MusiqueEQDSPTests_artefacts\Release\MusiqueEQDSPTests.exe \| Select-Object -Last 8` | PASS, `[SUMMARY] checks=619 failures=0` |

Coverage notable:
- EQ 5 bandes, HPF/LPF, slopes 12/24/48;
- graph mapping et hit-tests UI helpers;
- migration presets legacy;
- 8 presets factory beta depuis fichier source;
- 8 presets factory beta depuis `BinaryData`;
- mix, output, bypass, mono/stereo;
- stabilite numerique multi sample rates/block sizes;
- automation rapide;
- SAFE/TRIM.

## Pluginval

Command:

```powershell
.\fx-eq\Tools\run-eq-pluginval.ps1 -DebugBuildRoot .\build-codex-fx-suite-ninja -ReleaseBuildRoot .\build-codex-fx-suite-ninja-release -StrictnessLevels 5,10
```

Executable: `D:\Dev\Projects\musique\FX\.tools\pluginval\bin\pluginval.exe`  
Summary: `D:\Dev\Projects\musique\FX\.tools\pluginval\logs\eq-pluginval-summary.json`

| Config | Strictness | Exit Code | Result | Log |
| --- | ---: | ---: | --- | --- |
| Debug | 5 | 0 | PASS | `.tools\pluginval\logs\pluginval-debug-strictness-5.log` |
| Release | 5 | 0 | PASS | `.tools\pluginval\logs\pluginval-release-strictness-5.log` |
| Debug | 10 | 0 | PASS | `.tools\pluginval\logs\pluginval-debug-strictness-10.log` |
| Release | 10 | 0 | PASS | `.tools\pluginval\logs\pluginval-release-strictness-10.log` |

Strictness 5 est le gate bloquant. Strictness 10 passe aussi.

## Installer EQ Release

Command:

```powershell
.\installer\build-installers.ps1 -BuildRoot .\build-codex-fx-suite-ninja-release -PluginId eq -Config Release
```

Resultat: **PASS**  
Output: `build-codex-fx-suite-ninja-release\installer\output\Musique-EQ-and-Filter-Setup.exe`  
Size: `3999769` bytes

Verification `.iss`:

```powershell
rg -n "Musique EQ and Filter\.vst3|Musique EQ and Filter\.exe|fx-eq\\Presets" .\build-codex-fx-suite-ninja-release\installer\output\scripts\eq.iss
```

Resultat: **PASS**

Key lines:
- VST3 Release: `Musique EQ and Filter.vst3\*`
- Standalone Release: `Musique EQ and Filter.exe`
- Presets: `Source: "D:\Dev\Projects\musique\FX\fx-eq\Presets\*"; DestDir: "{app}\FX\fx-eq\Presets"`

## Manual QA Status

Non execute dans cette passe.

Items qui restent a valider manuellement avec audio reel:
- lancer le standalone Debug;
- lancer le standalone Release;
- verifier input/output, meters, clip LED;
- verifier bypass transparent, mono/stereo, mix 0/100, output low/nominal/hot;
- tester `Manual State`, selection factory, prev/next, save user preset, reload;
- tester drag 5 bandes frequence/gain, Q min/max, HPF/LPF off/on, slopes 12/24/48;
- tester SAFE/TRIM avec boosts empiles puis retour neutre;
- verifier absence d'overlap ou d'etat UI stale.

## Remaining Risks

- La QA audio/UI manuelle reste le seul blocker beta connu.
- Le fallback AppData des presets utilisateur est code et inclus dans les chemins EQ, mais la sauvegarde/reload via UI installee doit etre confirmee pendant la QA manuelle.
- Les warnings CMake/JUCE `CMP0175` restent presents dans la generation, sans echec de build.

## Completion Level

**Termine mais validation partielle.**  
Le gate technique est ferme et verifie. Le gate beta complet reste ouvert jusqu'a validation manuelle audio/UI.
