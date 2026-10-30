# Builds launchme-<version>-win64-setup.exe (installer/launchme.iss) into build\installer.
# -SkipBuild packs the existing build\vs instead of running tools\build.ps1 first.
# -Sign signs Setup and its uninstaller: $env:LAUNCHME_SIGN_COMMAND is the command ($f is the file),
# else signtool on PATH with the best certificate in the store and an RFC 3161 timestamp.
param([switch]$SkipBuild, [switch]$Sign)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$runtime = Join-Path $root 'build\vs'
$output = Join-Path $root 'build\installer'

if (!$SkipBuild) {
    & (Join-Path $PSScriptRoot 'build.ps1')
    if ($LASTEXITCODE -and $LASTEXITCODE -ne 0) { throw "build.ps1 failed ($LASTEXITCODE)" }
}
if (!(Test-Path -LiteralPath (Join-Path $runtime 'launchme.exe'))) { throw "No build to pack: $runtime\launchme.exe" }

# The version comes from project() in CMakeLists.txt, as the EXE's VERSIONINFO does.
$match = Select-String -LiteralPath (Join-Path $root 'CMakeLists.txt') -Pattern 'project\(launchme VERSION (\d+\.\d+\.\d+)'
if (!$match) { throw 'No version in CMakeLists.txt project()' }
$version = $match.Matches[0].Groups[1].Value

$candidates = @($env:ISCC)
foreach ($folder in @($env:ProgramFiles, ${env:ProgramFiles(x86)}, (Join-Path $env:LOCALAPPDATA 'Programs'))) {
    if ($folder) { $candidates += @((Join-Path $folder 'Inno Setup 7\ISCC.exe'), (Join-Path $folder 'Inno Setup 6\ISCC.exe')) }
}
$iscc = $candidates | Where-Object { $_ -and (Test-Path -LiteralPath $_) } | Select-Object -First 1
if (!$iscc) { $iscc = (Get-Command ISCC.exe -ErrorAction SilentlyContinue).Source }
if (!$iscc) { throw 'Inno Setup 6.6+ not found (winget install JRSoftware.InnoSetup, or set $env:ISCC)' }

$arguments = @("/DAppVersion=$version", "/DSourceDir=$runtime", "/DOutputDir=$output")
if ($Sign) {
    $command = if ($env:LAUNCHME_SIGN_COMMAND) { $env:LAUNCHME_SIGN_COMMAND } else { 'signtool.exe sign /fd sha256 /tr http://timestamp.digicert.com /td sha256 /a $f' }
    $arguments += @('/DSign', "/Slaunchme=$command")
}
& $iscc @arguments (Join-Path $root 'installer\launchme.iss')
if ($LASTEXITCODE -ne 0) { throw "ISCC failed ($LASTEXITCODE)" }

$setup = Join-Path $output "launchme-$version-win64-setup.exe"
"Ready: $setup"
"SHA-256: $((Get-FileHash -Algorithm SHA256 -LiteralPath $setup).Hash.ToLowerInvariant())"
