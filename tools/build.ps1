param([switch]$Test)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$vs = Join-Path ${env:ProgramFiles} 'Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin'
$cmake = Join-Path $vs 'cmake.exe'
$ctest = Join-Path $vs 'ctest.exe'
$work = Join-Path $root 'build\temp\vs'
$runtime = Join-Path $root 'build\vs'

function Invoke-BuildTool([string]$Tool, [string[]]$Arguments) {
    & $Tool @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Tool failed ($LASTEXITCODE)" }
}

if (!(Test-Path -LiteralPath $cmake)) { throw "Visual Studio 2026 CMake not found: $cmake" }
if (!(Test-Path -LiteralPath $root -PathType Container)) { throw "Repository directory not found: $root" }
Invoke-BuildTool $cmake @('-E', 'make_directory', (Join-Path $root 'build\temp'))

# Migrate the old in-place CMake build without deleting its files or captures.
if (Test-Path -LiteralPath (Join-Path $runtime 'CMakeCache.txt')) {
    $archive = Join-Path $root ('build\temp\previous-' + [guid]::NewGuid().ToString('N'))
    Invoke-BuildTool $cmake @('-E', 'rename', $runtime, $archive)
}

Invoke-BuildTool $cmake @('-S', $root, '-B', $work, '-G', 'Visual Studio 18 2026', '-A', 'x64', '-DLAUNCHME_STATIC_RUNTIME=ON')
if ($Test) {
    Invoke-BuildTool $cmake @('--build', $work, '--config', 'Release', '--parallel')
    Invoke-BuildTool $ctest @('--test-dir', $work, '-C', 'Release', '--output-on-failure', '-j', '8')
} else {
    Invoke-BuildTool $cmake @('--build', $work, '--config', 'Release', '--target', 'launchme', '--parallel')
}
Invoke-BuildTool $cmake @('--install', $work, '--config', 'Release', '--component', 'Runtime', '--prefix', $runtime)
# Extras built only on this machine (local/CMakeLists.txt); installs nothing without them.
Invoke-BuildTool $cmake @('--install', $work, '--config', 'Release', '--component', 'Local', '--prefix', $runtime)
"Ready: $(Join-Path $runtime 'launchme.exe')"
