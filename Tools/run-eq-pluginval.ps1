param(
    [string] $DownloadRoot = "D:\tmp\pluginval",
    [int[]] $StrictnessLevels = @(5, 10)
)

$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$buildRoot = Resolve-Path (Join-Path $repoRoot "..\build")
$logRoot = Join-Path $DownloadRoot "logs"
$summaryPath = Join-Path $logRoot "eq-pluginval-summary.json"

$plugins = @(
    @{
        Config = "Debug"
        Path = Join-Path $buildRoot "fx-eq\MusiqueEQ_artefacts\Debug\VST3\Musique EQ & Filter.vst3"
    },
    @{
        Config = "Release"
        Path = Join-Path $buildRoot "fx-eq\MusiqueEQ_artefacts\Release\VST3\Musique EQ & Filter.vst3"
    }
)

function Find-PluginvalExecutable {
    $pathCommand = Get-Command pluginval -ErrorAction SilentlyContinue
    if ($null -ne $pathCommand) {
        return $pathCommand.Source
    }

    if (Test-Path $DownloadRoot) {
        $downloaded = Get-ChildItem -LiteralPath $DownloadRoot -Recurse -Filter pluginval.exe -ErrorAction SilentlyContinue |
            Select-Object -First 1

        if ($null -ne $downloaded) {
            return $downloaded.FullName
        }
    }

    return $null
}

function Install-Pluginval {
    New-Item -ItemType Directory -Force -Path $DownloadRoot | Out-Null

    $existing = Find-PluginvalExecutable
    if ($null -ne $existing) {
        return $existing
    }

    $zipPath = Join-Path $DownloadRoot "pluginval_Windows.zip"
    $extractPath = Join-Path $DownloadRoot "expanded"
    $apiUrl = "https://api.github.com/repos/Tracktion/pluginval/releases/latest"
    $fallbackUrl = "https://github.com/Tracktion/pluginval/releases/download/v1.0.4/pluginval_Windows.zip"
    $downloadUrl = $fallbackUrl

    try {
        $release = Invoke-RestMethod -Uri $apiUrl -Headers @{ "User-Agent" = "MusiqueEQPluginvalGate" }
        $asset = $release.assets |
            Where-Object { $_.name -eq "pluginval_Windows.zip" -or $_.name -match "Windows.*\.zip$" } |
            Select-Object -First 1

        if ($null -ne $asset -and -not [string]::IsNullOrWhiteSpace($asset.browser_download_url)) {
            $downloadUrl = $asset.browser_download_url
        }
    }
    catch {
        Write-Warning "Could not query latest pluginval release; falling back to v1.0.4 Windows asset. $($_.Exception.Message)"
    }

    Write-Host "Downloading pluginval from $downloadUrl"
    Invoke-WebRequest -Uri $downloadUrl -OutFile $zipPath -Headers @{ "User-Agent" = "MusiqueEQPluginvalGate" }

    if (Test-Path $extractPath) {
        Remove-Item -LiteralPath $extractPath -Recurse -Force
    }

    New-Item -ItemType Directory -Force -Path $extractPath | Out-Null
    Expand-Archive -LiteralPath $zipPath -DestinationPath $extractPath -Force

    $executable = Get-ChildItem -LiteralPath $extractPath -Recurse -Filter pluginval.exe -ErrorAction Stop |
        Select-Object -First 1

    if ($null -eq $executable) {
        throw "pluginval.exe was not found after extracting $zipPath"
    }

    return $executable.FullName
}

function Invoke-Pluginval {
    param(
        [string] $PluginvalExe,
        [string] $Config,
        [string] $PluginPath,
        [int] $Strictness
    )

    if (-not (Test-Path $PluginPath)) {
        throw "Missing VST3 artifact for $Config`: $PluginPath"
    }

    $logPath = Join-Path $logRoot ("pluginval-{0}-strictness-{1}.log" -f $Config.ToLowerInvariant(), $Strictness)
    $arguments = @("--strictness-level", "$Strictness", $PluginPath)
    Write-Host "Running: `"$PluginvalExe`" $($arguments -join ' ')"

    $output = @(& $PluginvalExe @arguments 2>&1)
    $exitCode = if ($null -ne $LASTEXITCODE) { $LASTEXITCODE } else { 0 }
    $logLines = @(
        "Command: `"$PluginvalExe`" $($arguments -join ' ')",
        "Config: $Config",
        "Strictness: $Strictness",
        "Plugin: $PluginPath",
        "ExitCode: $exitCode",
        "",
        "Output:"
    ) + $output

    $logLines | Set-Content -LiteralPath $logPath

    $tail = @($logLines | Select-Object -Last 30)

    return [pscustomobject] @{
        Config = $Config
        Strictness = $Strictness
        PluginPath = $PluginPath
        ExitCode = $exitCode
        Passed = ($exitCode -eq 0)
        LogPath = $logPath
        Tail = $tail
    }
}

New-Item -ItemType Directory -Force -Path $logRoot | Out-Null

$pluginvalExe = Install-Pluginval
$results = @()
$strictness5Passed = $true

foreach ($plugin in $plugins) {
    $result = Invoke-Pluginval -PluginvalExe $pluginvalExe -Config $plugin.Config -PluginPath $plugin.Path -Strictness 5
    $results += $result
    if (-not $result.Passed) {
        $strictness5Passed = $false
    }
}

if ($strictness5Passed -and ($StrictnessLevels -contains 10)) {
    foreach ($plugin in $plugins) {
        $results += Invoke-Pluginval -PluginvalExe $pluginvalExe -Config $plugin.Config -PluginPath $plugin.Path -Strictness 10
    }
}

$summary = [pscustomobject] @{
    Timestamp = (Get-Date).ToString("yyyy-MM-dd HH:mm:ss zzz")
    Pluginval = $pluginvalExe
    BlockingStrictness = 5
    BlockingPassed = $strictness5Passed
    Results = $results
}

$summary | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $summaryPath

Write-Host "Summary: $summaryPath"
foreach ($result in $results) {
    $status = if ($result.Passed) { "PASS" } else { "FAIL" }
    Write-Host ("{0} strictness {1}: {2} (exit {3})" -f $result.Config, $result.Strictness, $status, $result.ExitCode)
}

if (-not $strictness5Passed) {
    exit 1
}

exit 0
