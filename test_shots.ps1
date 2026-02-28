Write-Host "Testing /shots API..."
$r = Invoke-WebRequest -Uri "http://127.0.0.1:8081/shots" -UseBasicParsing -TimeoutSec 5
Write-Host "Status:" $r.StatusCode
Write-Host "Content:" $r.Content.Substring(0, [Math]::Min(500, $r.Content.Length))

Write-Host ""
Write-Host "Testing /state API (camera info)..."
$s = Invoke-WebRequest -Uri "http://127.0.0.1:8081/state" -UseBasicParsing -TimeoutSec 5
$data = $s.Content | ConvertFrom-Json
Write-Host "cameraA:" $data.cameraA
Write-Host "cameraB:" $data.cameraB
Write-Host "isScoring:" $data.isScoring
Write-Host "guidedCalib:" ($data.guidedCalib | ConvertTo-Json -Compress)
