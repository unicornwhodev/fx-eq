param(
    [string] $DebugBuildRoot = '',
    [string] $ReleaseBuildRoot = '',
    [int[]] $StrictnessLevels = @(5, 10)
)

$ErrorActionPreference = 'Stop'

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot '..\..')
if ([string]::IsNullOrWhiteSpace($DebugBuildRoot)) {
    $DebugBuildRoot = Join-Path $repoRoot 'build-codex-fx-suite-ninja'
}
if ([string]::IsNullOrWhiteSpace($ReleaseBuildRoot)) {
    $ReleaseBuildRoot = Join-Path $repoRoot 'build-codex-fx-suite-ninja-release'
}

if (-not ($StrictnessLevels -contains 5)) {
    throw 'Strictness level 5 is required because it is the blocking pluginval beta gate.'
}

$logRoot = Join-Path $repoRoot '.tools\pluginval\logs'
$summaryPath = Join-Path $logRoot 'eq-pluginval-summary.json'

function Resolve-FullPath([string] $Path) {
    $executionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Path)
}

function Find-PluginvalExecutable {
    $pathCommand = Get-Command pluginval -ErrorAction SilentlyContinue
    if ($null -ne $pathCommand) {
        return $pathCommand.Source
    }

    $repoLocalDir = Join-Path $repoRoot '.tools\pluginval\bin'
    $repoLocal = Join-Path $repoLocalDir 'pluginval.exe'
    if (Test-Path -Path $repoLocal -PathType Leaf) {
        $resolvedLocalDir = Resolve-FullPath $repoLocalDir
        $pathEntries = @($env:PATH -split [IO.Path]::PathSeparator | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
        if ($pathEntries -notcontains $resolvedLocalDir) {
            $env:PATH = $resolvedLocalDir + [IO.Path]::PathSeparator + $env:PATH
        }

        return (Resolve-FullPath $repoLocal)
    }

    throw 'pluginval.exe was not found in PATH or .tools\pluginval\bin. Install pluginval before running this gate.'
}

function New-PluginTarget {
    param(
        [string] $Config,
        [string] $BuildRoot
    )

    $resolvedBuildRoot = Resolve-FullPath $BuildRoot
    $pluginPath = Join-Path $resolvedBuildRoot "fx-eq\MusiqueEQ_artefacts\$Config\VST3\Musique EQ and Filter.vst3"

    [pscustomobject] @{
        Config = $Config
        BuildRoot = $resolvedBuildRoot
        Path = $pluginPath
    }
}

function Invoke-Pluginval {
    param(
        [string] $PluginvalExe,
        [pscustomobject] $Plugin,
        [int] $Strictness
    )

    if (-not (Test-Path -Path $Plugin.Path -PathType Container)) {
        throw "Missing current VST3 artifact for $($Plugin.Config): $($Plugin.Path)"
    }

    $logPath = Join-Path $logRoot ("pluginval-{0}-strictness-{1}.log" -f $Plugin.Config.ToLowerInvariant(), $Strictness)
    $arguments = @('--strictness-level', "$Strictness", $Plugin.Path)
    Write-Host "Running: `"$PluginvalExe`" $($arguments -join ' ')"

    $output = @(& $PluginvalExe @arguments 2>&1)
    $exitCode = if ($null -ne $LASTEXITCODE) { $LASTEXITCODE } else { 0 }
    $logLines = @(
        "Command: `"$PluginvalExe`" $($arguments -join ' ')",
        "Config: $($Plugin.Config)",
        "Strictness: $Strictness",
        "BuildRoot: $($Plugin.BuildRoot)",
        "Plugin: $($Plugin.Path)",
        "ExitCode: $exitCode",
        '',
        'Output:'
    ) + $output

    $logLines | Set-Content -LiteralPath $logPath

    [pscustomobject] @{
        Config = $Plugin.Config
        Strictness = $Strictness
        BuildRoot = $Plugin.BuildRoot
        PluginPath = $Plugin.Path
        ExitCode = $exitCode
        Passed = ($exitCode -eq 0)
        LogPath = $logPath
        Tail = @($logLines | Select-Object -Last 30)
    }
}

New-Item -ItemType Directory -Force -Path $logRoot | Out-Null

$pluginvalExe = Find-PluginvalExecutable
$plugins = @(
    New-PluginTarget -Config 'Debug' -BuildRoot $DebugBuildRoot
    New-PluginTarget -Config 'Release' -BuildRoot $ReleaseBuildRoot
)

$results = @()
$strictness5Passed = $true
foreach ($plugin in $plugins) {
    $result = Invoke-Pluginval -PluginvalExe $pluginvalExe -Plugin $plugin -Strictness 5
    $results += $result
    if (-not $result.Passed) {
        $strictness5Passed = $false
    }
}

if ($strictness5Passed -and ($StrictnessLevels -contains 10)) {
    foreach ($plugin in $plugins) {
        $results += Invoke-Pluginval -PluginvalExe $pluginvalExe -Plugin $plugin -Strictness 10
    }
}

$summary = [pscustomobject] @{
    Timestamp = (Get-Date).ToString('yyyy-MM-dd HH:mm:ss zzz')
    Pluginval = $pluginvalExe
    BlockingStrictness = 5
    BlockingPassed = $strictness5Passed
    Results = $results
}

$summary | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $summaryPath

Write-Host "Summary: $summaryPath"
foreach ($result in $results) {
    $status = if ($result.Passed) { 'PASS' } else { 'FAIL' }
    Write-Host ('{0} strictness {1}: {2} (exit {3})' -f $result.Config, $result.Strictness, $status, $result.ExitCode)
}

if (-not $strictness5Passed) {
    exit 1
}

exit 0
