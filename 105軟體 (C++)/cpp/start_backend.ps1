$env:PATH = "C:\msys64\mingw64\bin;" + $env:PATH
$exe = Join-Path $PSScriptRoot "t91_tracker.exe"
Write-Host "Starting: $exe"
Start-Process -FilePath $exe -WorkingDirectory $PSScriptRoot
Write-Host "Backend started."
