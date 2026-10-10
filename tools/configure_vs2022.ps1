$ErrorActionPreference = "Stop"
Push-Location (Join-Path $PSScriptRoot "..")
try { cmake --preset vs2022-x64 } finally { Pop-Location }
