$flag = Join-Path $PSScriptRoot "no_camera.flag"
if (Test-Path $flag) {
    Remove-Item $flag -Force
    Write-Host "Deleted: $flag"
} else {
    Write-Host "Not found: $flag"
}
