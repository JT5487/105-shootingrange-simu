$r = Invoke-WebRequest -Uri "http://127.0.0.1:8081/shots" -UseBasicParsing -TimeoutSec 5
Write-Host $r.Content
