param(
    [string]$BuildDir = (Join-Path $PSScriptRoot '../build/release'),
    [string]$IsccPath = $env:ISCC_PATH
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$BuildDir = (Resolve-Path -LiteralPath $BuildDir).Path
foreach ($name in @('FL Plugin Atlas.clap', 'atlas.exe')) {
    if (-not (Test-Path -LiteralPath (Join-Path $BuildDir $name))) {
        throw "Missing release binary: $name"
    }
}
if (-not $IsccPath) {
    $command = Get-Command ISCC.exe -ErrorAction SilentlyContinue
    if ($command) { $IsccPath = $command.Source }
}
if (-not $IsccPath) {
    $candidates = @(
        (Join-Path ${env:ProgramFiles(x86)} 'Inno Setup 6/ISCC.exe'),
        (Join-Path $env:ProgramFiles 'Inno Setup 7/ISCC.exe')
    )
    $IsccPath = $candidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
}
if (-not $IsccPath) {
    throw 'Install Inno Setup 6.3 or later, or pass -IsccPath pointing to ISCC.exe.'
}
& $IsccPath "/DBuildDir=$BuildDir" (Join-Path $PSScriptRoot 'FLPluginAtlas.iss')
if ($LASTEXITCODE) { throw "Installer compilation failed: $LASTEXITCODE" }
$output = Join-Path $projectRoot 'build/installer'
$installer = Join-Path $output 'FL-Plugin-Atlas-0.1.0-Windows-x64-Setup.exe'
$hash = (Get-FileHash -LiteralPath $installer -Algorithm SHA256).Hash.ToLowerInvariant()
"$hash  $([IO.Path]::GetFileName($installer))" | Set-Content -LiteralPath (Join-Path $output 'SHA256SUMS.txt') -Encoding ascii
Get-Item -LiteralPath $installer
