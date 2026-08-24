param(
    [string]$Configuration = "Release",
    [string]$BuildDir = "build",
    [string]$JuceDir = "",
    [switch]$BootstrapJuce,
    [switch]$SkipBuild,
    [switch]$SkipInstaller,
    [string]$InnoSetupPath = "",
    [string]$AppVersion = ""
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest
$repoRoot = Split-Path -Parent $PSCommandPath
$repoSlug = Split-Path -Leaf $repoRoot
$cmakeText = Get-Content -LiteralPath (Join-Path $repoRoot "CMakeLists.txt") -Raw
$projectMatch = [regex]::Match($cmakeText, 'project\(([^\s\)]+)\s+VERSION\s+([0-9]+(?:\.[0-9]+){0,3})')
$productMatch = [regex]::Match($cmakeText, 'PRODUCT_NAME\s+"([^"]+)"')
if (-not $projectMatch.Success -or -not $productMatch.Success) { throw "Unable to detect project metadata." }
$target = $projectMatch.Groups[1].Value
$version = if ($AppVersion) { $AppVersion } else { $projectMatch.Groups[2].Value }
$productName = $productMatch.Groups[1].Value
$buildPath = if ([System.IO.Path]::IsPathRooted($BuildDir)) { $BuildDir } else { Join-Path $repoRoot $BuildDir }

if (-not $SkipBuild) {
    $buildArgs = @{ Configuration = $Configuration; BuildDir = $BuildDir }
    if ($JuceDir) { $buildArgs["JuceDir"] = $JuceDir }
    if ($BootstrapJuce) { $buildArgs["BootstrapJuce"] = $true }
    & (Join-Path $repoRoot "_build_all.ps1") @buildArgs
    if ($LASTEXITCODE -ne 0) { throw "Build failed." }
}
$artifactRoot = Join-Path $buildPath "${target}_artefacts\$Configuration"
$standaloneExe = Get-ChildItem (Join-Path $artifactRoot "Standalone") -File -Filter *.exe | Select-Object -First 1
$vst3Bundle = Get-ChildItem (Join-Path $artifactRoot "VST3") -Directory -Filter *.vst3 | Select-Object -First 1
if (-not $standaloneExe -or -not $vst3Bundle) { throw "Release artifacts are missing." }

$releaseDir = Join-Path $repoRoot "release"
$stageDir = Join-Path $releaseDir "staging"
if (Test-Path $stageDir) { Remove-Item $stageDir -Recurse -Force }
$standaloneStage = Join-Path $stageDir "Standalone"
$vst3Stage = Join-Path $stageDir "VST3"
New-Item -ItemType Directory -Path $standaloneStage -Force | Out-Null
New-Item -ItemType Directory -Path $vst3Stage -Force | Out-Null
Copy-Item $standaloneExe.FullName $standaloneStage -Force
Copy-Item $vst3Bundle.FullName $vst3Stage -Recurse -Force

$presets = Join-Path $repoRoot "Presets"
if (Test-Path $presets) {
    Copy-Item $presets (Join-Path $standaloneStage "Presets") -Recurse -Force
    $copiedVst3 = Join-Path $vst3Stage $vst3Bundle.Name
    Copy-Item $presets (Join-Path $copiedVst3 "Presets") -Recurse -Force
}

New-Item -ItemType Directory -Path $releaseDir -Force | Out-Null
$zipPath = Join-Path $releaseDir "${repoSlug}_${version}_Windows_x64_Portable.zip"
Compress-Archive -Path (Join-Path $stageDir "*") -DestinationPath $zipPath -Force
Write-Host "Portable package: $zipPath"

if (-not $SkipInstaller) {
    $candidates = @()
    if ($InnoSetupPath) { $candidates += $InnoSetupPath }
    $cmd = Get-Command ISCC.exe -ErrorAction SilentlyContinue
    if ($cmd) { $candidates += $cmd.Source }
    $candidates += @("C:\Program Files (x86)\Inno Setup 6\ISCC.exe", "C:\Program Files\Inno Setup 6\ISCC.exe", "D:\InnoSetup6\ISCC.exe")
    $iscc = $candidates | Where-Object { $_ -and (Test-Path -LiteralPath $_) } | Select-Object -First 1
    if (-not $iscc) { throw "Inno Setup 6 not found. Use -SkipInstaller for ZIP-only packaging or -InnoSetupPath <ISCC.exe>." }
    $iss = Join-Path $repoRoot "installer\MusiqueFX.iss"
    $args = @(
        "/DAppName=$productName",
        "/DAppVersion=$version",
        "/DRepoSlug=$repoSlug",
        "/DStandaloneSource=$($standaloneExe.FullName)",
        "/DStandaloneExeName=$($standaloneExe.Name)",
        "/DVst3Source=$($vst3Bundle.FullName)",
        "/DVst3DirName=$($vst3Bundle.Name)",
        "/DPresetsSource=$presets",
        "/DOutputDir=$releaseDir",
        "/DOutputBaseFilename=${repoSlug}_${version}_Windows_x64_Setup",
        $iss)
    & $iscc @args
    if ($LASTEXITCODE -ne 0) { throw "Inno Setup failed." }
}
