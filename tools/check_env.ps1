$ErrorActionPreference = "Stop"

Write-Host "=== VSTis Visual Studio 2022 environment check ==="

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) { Write-Host "[NG] vswhere.exe not found."; exit 1 }

$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) {
    Write-Host "[NG] Visual Studio C++ toolchain not found."
    Write-Host "Visual Studio Installer -> Modify -> Desktop development with C++"
    exit 1
}
Write-Host "[OK] Visual Studio C++: $vs"

if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) { Write-Host "[NG] cmake not found."; exit 1 }
Write-Host "[OK] $(& cmake --version | Select-Object -First 1)"

if (-not (Get-Command git -ErrorAction SilentlyContinue)) { Write-Host "[NG] git not found."; exit 1 }
Write-Host "[OK] $(& git --version)"

if (Get-Command dotnet -ErrorAction SilentlyContinue) {
    Write-Host "[INFO] .NET: $(& dotnet --version) (not required by VST3 builds)"
}

Write-Host "Environment looks ready."
