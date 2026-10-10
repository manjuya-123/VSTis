$ErrorActionPreference = "Stop"

Push-Location (Join-Path $PSScriptRoot "..")
try {
    cmake --preset vs2022-core
    cmake --build --preset core-release
    ctest --test-dir build/vs2022-core -C Release --output-on-failure

    $audioDir = "build/vs2022-core/instruments/FiddleModel/regression-audio"
    Write-Host "=== FiddleModel audio regression metrics ==="

    @(
        "metrics.csv",
        "gesture_onset_metrics.csv",
        "gesture_release_metrics.csv",
        "gesture_reversal_metrics.csv"
    ) | ForEach-Object {
        $path = Join-Path $audioDir $_
        if (Test-Path $path) {
            Write-Host "--- $_ ---"
            Get-Content $path
        }
    }

    Write-Host "Listening WAVs: $audioDir"
}
finally {
    Pop-Location
}
