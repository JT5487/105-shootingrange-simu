# T91 System Ultimate Launcher - Virtual Drive Strategy
# Last Modified: 2026/02/28
Add-Type -AssemblyName System.Windows.Forms
Add-Type -TypeDefinition @"
using System;
using System.Text;
using System.Runtime.InteropServices;
public class WinApi {
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern IntPtr FindWindow(string lpClassName, string lpWindowName);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetWindowText(IntPtr hWnd, StringBuilder lpString, int nMaxCount);
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr hWnd, IntPtr hWndInsertAfter, int X, int Y, int cx, int cy, uint uFlags);
    [DllImport("user32.dll")] public static extern bool MoveWindow(IntPtr hWnd, int X, int Y, int nWidth, int nHeight, bool bRepaint);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumWindowsProc lpEnumFunc, IntPtr lParam);
    public delegate bool EnumWindowsProc(IntPtr hWnd, IntPtr lParam);
    public static readonly IntPtr HWND_TOP = new IntPtr(0);
    public const uint SWP_SHOWWINDOW = 0x0040;
}
"@

# 1. Setup Virtual Drive (X:) to bypass brackets/spaces in path
$scriptPath = Split-Path -Parent $MyInvocation.MyCommand.Path
subst X: /D 2>$null
subst X: "$scriptPath"
if (-not (Test-Path "X:\")) {
    Write-Error "Failed to create virtual drive X:. Please run as admin or check if X: is occupied."
    Read-Host "Press Enter to exit"; exit
}

# 2. Environment & Backend
$env:PATH = "C:\msys64\mingw64\bin;" + $env:PATH
Stop-Process -Name t91_tracker -Force -ErrorAction SilentlyContinue 2>$null
Stop-Process -Name chrome -Force -ErrorAction SilentlyContinue 2>$null

Write-Host "Starting Backend..."
# 在虛擬磁碟環境下，直接啟動執行檔並指定工作目錄是最穩定的
$backendProc = Start-Process "X:\cpp\t91_tracker.exe" -WorkingDirectory "X:\cpp" -WindowStyle Normal -PassThru
Start-Sleep -Seconds 3

# 3. Launching Frontends via X: Drive
$chromePath = 'C:\Program Files\Google\Chrome\Application\chrome.exe'
if (-not (Test-Path $chromePath)) { $chromePath = 'C:\Program Files (x86)\Google\Chrome\Application\chrome.exe' }

$script:foundHwnd = [IntPtr]::Zero
function Find-Window-By-Tag($tag) {
    $script:foundHwnd = [IntPtr]::Zero
    $enumProc = [WinApi+EnumWindowsProc] {
        param($hwnd, $lparam)
        $sb = New-Object System.Text.StringBuilder 256
        [WinApi]::GetWindowText($hwnd, $sb, $sb.Capacity) | Out-Null
        if ($sb.ToString() -like "*$tag*") { $script:foundHwnd = $hwnd; return $false }
        return $true
    }
    [WinApi]::EnumWindows($enumProc, [IntPtr]::Zero) | Out-Null
    return $script:foundHwnd
}

function Open-T91-X($file, $tag, $targetX, $targetY, $winW = 1920, $winH = 1080) {
    $url = "file:///X:/$file"
    $pPath = "X:\chrome_profiles\p_$($tag.Replace('[','').Replace(']',''))"
    if (Test-Path $pPath) { Remove-Item $pPath -Recurse -Force -ErrorAction SilentlyContinue }
    
    $args = @(
        "--app=$url",
        "--user-data-dir=$pPath",
        "--window-position=$targetX,$targetY",
        "--window-size=$winW,$winH",
        "--kiosk",
        "--autoplay-policy=no-user-gesture-required",
        "--allow-file-access-from-files",
        "--no-first-run",
        "--no-default-browser-check"
    )
    
    Write-Host "Opening $file at ($targetX, $targetY) ${winW}x${winH}"
    Start-Process $chromePath -ArgumentList $args
    
    for ($i = 0; $i -lt 15; $i++) {
        Start-Sleep -Milliseconds 800
        $hwnd = Find-Window-By-Tag $tag
        if ($hwnd -ne [IntPtr]::Zero) {
            [WinApi]::SetWindowPos($hwnd, [WinApi]::HWND_TOP, $targetX, $targetY, $winW, $winH, [WinApi]::SWP_SHOWWINDOW)
            [WinApi]::MoveWindow($hwnd, $targetX, $targetY, $winW, $winH, $true)
            return
        }
    }
}

# 4. 自動偵測螢幕配置
# 依 X 座標排序：左=投影幕A, 中=投影幕B, 右=控制台
$allScreens = [System.Windows.Forms.Screen]::AllScreens
$sorted = $allScreens | Sort-Object { $_.Bounds.X }

if ($sorted.Count -ge 3) {
    # 3 螢幕：X最小=電腦螢幕(控制台), 中=左投影機(A), X最大=右投影機(B)
    $ctrlX = $sorted[0].Bounds.X
    $ctrlY = $sorted[0].Bounds.Y
    $screenW = $sorted[0].Bounds.Width
    $screenH = $sorted[0].Bounds.Height
    $screenA_X = $sorted[1].Bounds.X
    $screenA_Y = $sorted[1].Bounds.Y
    $screenA_W = $sorted[1].Bounds.Width
    $screenA_H = $sorted[1].Bounds.Height
    $screenB_X = $sorted[2].Bounds.X
    $screenB_Y = $sorted[2].Bounds.Y
    $screenB_W = $sorted[2].Bounds.Width
    $screenB_H = $sorted[2].Bounds.Height
} elseif ($nonPrimary.Count -eq 1) {
    $screenA_X = $nonPrimary[0].Bounds.X
    $screenA_Y = $nonPrimary[0].Bounds.Y
    $screenA_W = $nonPrimary[0].Bounds.Width
    $screenA_H = $nonPrimary[0].Bounds.Height
    $screenB_X = $ctrlX + $screenW
    $screenB_Y = $ctrlY
    $screenB_W = $screenW
    $screenB_H = $screenH
    Write-Host "[WARNING] Only 1 non-primary screen detected. Screen B fallback." -ForegroundColor Yellow
} else {
    $screenA_X = $ctrlX + $screenW
    $screenA_Y = $ctrlY
    $screenA_W = $screenW
    $screenA_H = $screenH
    $screenB_X = $ctrlX + $screenW * 2
    $screenB_Y = $ctrlY
    $screenB_W = $screenW
    $screenB_H = $screenH
    Write-Host "[WARNING] No non-primary screens detected. Using offset positions." -ForegroundColor Yellow
}

Write-Host "Screen layout detected:" -ForegroundColor Cyan
Write-Host "  Controller: ($ctrlX, $ctrlY) ${screenW}x${screenH}" -ForegroundColor Gray
Write-Host "  Screen A:   ($screenA_X, $screenA_Y) ${screenA_W}x${screenA_H}" -ForegroundColor Gray
Write-Host "  Screen B:   ($screenB_X, $screenB_Y) ${screenB_W}x${screenB_H}" -ForegroundColor Gray

Write-Host "Positioning screens..."
Open-T91-X "index.html" "[T91_CTRL]" $ctrlX $ctrlY $screenW $screenH
Open-T91-X "training_screen_a.html" "[T91_A]" $screenA_X $screenA_Y $screenA_W $screenA_H
Open-T91-X "training_screen_b.html" "[T91_B]" $screenB_X $screenB_Y $screenB_W $screenB_H

Write-Host "-----------------------------"
Write-Host " ALL SYSTEMS GO (Virtual Drive Mode) "
Write-Host "-----------------------------"
Write-Host "NOTE: The system will automatically cleanup when you press 'Shutdown' in the UI."

# 重要：監控後端進程，若後端被關閉（由結束系統按鈕觸發），則腳本自動完成清理併退出
Wait-Process -Id $backendProc.Id -ErrorAction SilentlyContinue

Write-Host "Cleaning up Virtual Drive..."
subst X: /D 2>$null
Write-Host "System Exit."
Stop-Process -Id $PID # 結束自己
