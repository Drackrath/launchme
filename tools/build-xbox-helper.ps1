# Builds the Xbox account helper (helpers/xbox, Rust, on Xodus) and prints what
# src/store/xbox_account.cpp pins: its size and SHA-256. The published asset must be this file.
# -Install also copies it where Launchme looks for it (%LOCALAPPDATA%\Launchme\tools), for local testing.
# Needs rustup's stable toolchain (Rust 1.98+) and protoc on PATH (Xodus compiles .proto files).
param([switch]$Install)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$crate = Join-Path $root 'helpers\xbox'
$target = Join-Path $root 'build\temp\xbox-helper'
$version = (Select-String -LiteralPath (Join-Path $crate 'Cargo.toml') -Pattern '^version = "(.+)"').Matches[0].Groups[1].Value

foreach ($tool in 'cargo', 'protoc') {
    if (!(Get-Command $tool -ErrorAction SilentlyContinue)) { throw "$tool not found on PATH" }
}
$env:CARGO_TARGET_DIR = $target
# Cargo reports progress on stderr; Windows PowerShell would turn that into errors under 'Stop'.
$ErrorActionPreference = 'Continue'
& cargo build --release --locked --manifest-path (Join-Path $crate 'Cargo.toml')
if ($LASTEXITCODE -ne 0) { throw "cargo build failed ($LASTEXITCODE)" }
& cargo test --release --locked --manifest-path (Join-Path $crate 'Cargo.toml')
if ($LASTEXITCODE -ne 0) { throw "cargo test failed ($LASTEXITCODE)" }
$ErrorActionPreference = 'Stop'

$exe = Join-Path $target 'release\launchme-xbox.exe'
$published = Join-Path $root "build\launchme-xbox-$version.exe"
Copy-Item -LiteralPath $exe -Destination $published -Force
$bytes = (Get-Item -LiteralPath $published).Length
$sha = (Get-FileHash -LiteralPath $published -Algorithm SHA256).Hash.ToLowerInvariant()
"Helper:  $published"
"Version: $version"
"Bytes:   $bytes"
"SHA-256: $sha"
"Pin in src/store/xbox_account.hpp (helper_bytes) and .cpp (helper_digest)."

if ($Install) {
    $config = if ($env:LAUNCHME_CONFIG_DIR) { $env:LAUNCHME_CONFIG_DIR } else { Join-Path $env:LOCALAPPDATA 'Launchme' }
    $tools = Join-Path $config 'tools'
    New-Item -ItemType Directory -Force -Path $tools | Out-Null
    Copy-Item -LiteralPath $published -Destination (Join-Path $tools "launchme-xbox-$version.exe") -Force
    "Installed: $(Join-Path $tools "launchme-xbox-$version.exe")"
}
