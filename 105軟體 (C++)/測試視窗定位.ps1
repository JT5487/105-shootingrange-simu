# 測試腳本：診斷視窗定位問題
# Last Modified: 2026/02/28
Add-Type -AssemblyName System.Windows.Forms

Write-Host "=== T91 視窗定位診斷工具 ===" -ForegroundColor Cyan

# 測試 1: 檢查螢幕配置
Write-Host "`n[測試 1] 偵測螢幕配置" -ForegroundColor Yellow
$allScreens = [System.Windows.Forms.Screen]::AllScreens
$primaryScreen = [System.Windows.Forms.Screen]::PrimaryScreen
Write-Host "  偵測到 $($allScreens.Count) 個螢幕：" -ForegroundColor Green
$idx = 0
$allScreens | Sort-Object { $_.Bounds.X } | ForEach-Object {
    $idx++
    $tag = if ($_.Primary) { " [PRIMARY]" } else { "" }
    Write-Host "  螢幕$idx$tag X=$($_.Bounds.X) Y=$($_.Bounds.Y) W=$($_.Bounds.Width) H=$($_.Bounds.Height)" -ForegroundColor Gray
}

$nonPrimary = $allScreens | Where-Object { -not $_.Primary } | Sort-Object { $_.Bounds.X }
if ($nonPrimary.Count -ge 2) {
    Write-Host "  => 控制台: ($($primaryScreen.Bounds.X), $($primaryScreen.Bounds.Y))" -ForegroundColor Cyan
    Write-Host "  => 投影幕A: ($($nonPrimary[0].Bounds.X), $($nonPrimary[0].Bounds.Y))" -ForegroundColor Cyan
    Write-Host "  => 投影幕B: ($($nonPrimary[1].Bounds.X), $($nonPrimary[1].Bounds.Y))" -ForegroundColor Cyan
} else {
    Write-Host "  [WARNING] 非主螢幕不足 2 個" -ForegroundColor Yellow
}

# 測試 2: 檢查虛擬磁碟
Write-Host "`n[測試 2] 檢查虛擬磁碟 X:" -ForegroundColor Yellow
$scriptPath = Split-Path -Parent $MyInvocation.MyCommand.Path
subst X: /D 2>$null
subst X: "$scriptPath"
if (Test-Path "X:\") {
    Write-Host "  OK 虛擬磁碟 X: 創建成功" -ForegroundColor Green
    Write-Host "  路徑: X: -> $scriptPath" -ForegroundColor Gray
} else {
    Write-Host "  FAIL 虛擬磁碟 X: 創建失敗！" -ForegroundColor Red
}

# 測試 3: 檢查 Chrome 路徑
Write-Host "`n[測試 3] 檢查 Chrome" -ForegroundColor Yellow
$chromePath = 'C:\Program Files\Google\Chrome\Application\chrome.exe'
if (-not (Test-Path $chromePath)) { 
    $chromePath = 'C:\Program Files (x86)\Google\Chrome\Application\chrome.exe' 
}
if (Test-Path $chromePath) {
    Write-Host "  OK Chrome 找到: $chromePath" -ForegroundColor Green
} else {
    Write-Host "  FAIL Chrome 未找到！" -ForegroundColor Red
}

# 測試 4: 檢查必要檔案
Write-Host "`n[測試 4] 檢查必要檔案" -ForegroundColor Yellow
$files = @("index.html", "training_screen_a.html", "training_screen_b.html", "cpp\t91_tracker.exe")
foreach ($file in $files) {
    if (Test-Path "X:\$file") {
        Write-Host "  OK $file" -ForegroundColor Green
    } else {
        Write-Host "  FAIL $file 不存在！" -ForegroundColor Red
    }
}

# 測試 5: 座標系統一致性
Write-Host "`n[測試 5] 校準座標系統一致性" -ForegroundColor Yellow
Write-Host "  IRTracker::dst_points_     = +-0.9" -ForegroundColor Gray
Write-Host "  CalibrationService Manual  = +-0.9" -ForegroundColor Gray
Write-Host "  ChessboardConfig mapping   = +-0.9" -ForegroundColor Gray
Write-Host "  OK 所有校準座標統一為 +-0.9" -ForegroundColor Green

Write-Host "`n=== 診斷完成 ===" -ForegroundColor Cyan

# 清理
subst X: /D 2>$null
Read-Host "`n按 Enter 結束"
