$ErrorActionPreference = "Stop"
Push-Location (Join-Path $PSScriptRoot "..")
try { cmake --build --preset debug --target FiddleModel_Standalone FiddleModel_VST3 } finally { Pop-Location }
