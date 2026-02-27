# T91 System Launcher - Remote/Development Mode (No Kiosk)
# 適用於 RustDesk 等遠端桌面環境，不使用 kiosk 全螢幕
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

# 1. Virtual Drive
$scriptPath = Split-Path -Parent $MyInvocation.MyCommand.Path
subst X: /D 2>$null
subst X: "$scriptPath"
if (-not (Test-Path "X:\")) {
    Write-Error "Failed to create virtual drive X:"
    Read-Host "Press Enter to exit"; exit
}

# 2. Backend
$env:PATH = "C:\msys64\mingw64\bin;" + $env:PATH
Stop-Process -Name t91_tracker -Force -ErrorAction SilentlyContinue 2>$null
Stop-Process -Name chrome -Force -ErrorAction SilentlyContinue 2>$null

Write-Host "[Remote Mode] Starting Backend..." -ForegroundColor Cyan
$backendProc = Start-Process "X:\cpp\t91_tracker.exe" -WorkingDirectory "X:\cpp" -WindowStyle Normal -PassThru
Start-Sleep -Seconds 5

# 3. Chrome (non-kiosk)
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

function Open-T91-Remote($file, $tag, $targetX, $targetY, $winW, $winH) {
    $url = "file:///X:/$file"
    $pPath = "X:\chrome_profiles\p_$($tag.Replace('[','').Replace(']',''))"
    if (Test-Path $pPath) { Remove-Item $pPath -Recurse -Force -ErrorAction SilentlyContinue }

    $args = @(
        "--app=$url",
        "--user-data-dir=$pPath",
        "--window-position=$targetX,$targetY",
        "--window-size=$winW,$winH",
        "--autoplay-policy=no-user-gesture-required",
        "--allow-file-access-from-files",
        "--no-first-run",
        "--no-default-browser-check"
    )
    # 注意：不使用 --kiosk，方便遠端操作

    Write-Host "  Opening $file at ($targetX, $targetY) ${winW}x${winH}"
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
    Write-Host "  [WARN] Could not find window for $tag" -ForegroundColor Yellow
}

# 4. 偵測螢幕
$allScreens = [System.Windows.Forms.Screen]::AllScreens
$primaryScreen = [System.Windows.Forms.Screen]::PrimaryScreen
$nonPrimary = $allScreens | Where-Object { -not $_.Primary } | Sort-Object { $_.Bounds.X }

$ctrlX = $primaryScreen.Bounds.X
$ctrlY = $primaryScreen.Bounds.Y
$screenW = $primaryScreen.Bounds.Width
$screenH = $primaryScreen.Bounds.Height

if ($nonPrimary.Count -ge 2) {
    $screenA_X = $nonPrimary[0].Bounds.X
    $screenA_Y = $nonPrimary[0].Bounds.Y
    $screenA_W = $nonPrimary[0].Bounds.Width
    $screenA_H = $nonPrimary[0].Bounds.Height
    $screenB_X = $nonPrimary[1].Bounds.X
    $screenB_Y = $nonPrimary[1].Bounds.Y
    $screenB_W = $nonPrimary[1].Bounds.Width
    $screenB_H = $nonPrimary[1].Bounds.Height
} elseif ($nonPrimary.Count -eq 1) {
    $screenA_X = $nonPrimary[0].Bounds.X
    $screenA_Y = $nonPrimary[0].Bounds.Y
    $screenA_W = $nonPrimary[0].Bounds.Width
    $screenA_H = $nonPrimary[0].Bounds.Height
    $screenB_X = $ctrlX + $screenW
    $screenB_Y = $ctrlY
    $screenB_W = $screenW
    $screenB_H = $screenH
} else {
    # 單螢幕：三個視窗疊在同一螢幕，縮小顯示
    $screenA_X = $ctrlX
    $screenA_Y = $ctrlY
    $screenA_W = $screenW
    $screenA_H = $screenH
    $screenB_X = $ctrlX
    $screenB_Y = $ctrlY
    $screenB_W = $screenW
    $screenB_H = $screenH
    Write-Host "[Remote Mode] Single screen - windows will overlap. Use Alt-Tab." -ForegroundColor Yellow
}

Write-Host ""
Write-Host "Screen layout:" -ForegroundColor Cyan
Write-Host "  Controller: ($ctrlX, $ctrlY) ${screenW}x${screenH}" -ForegroundColor Gray
Write-Host "  Screen A:   ($screenA_X, $screenA_Y) ${screenA_W}x${screenA_H}" -ForegroundColor Gray
Write-Host "  Screen B:   ($screenB_X, $screenB_Y) ${screenB_W}x${screenB_H}" -ForegroundColor Gray
Write-Host ""

Write-Host "Launching windows (non-kiosk)..." -ForegroundColor Cyan
Open-T91-Remote "index.html" "[T91_CTRL]" $ctrlX $ctrlY $screenW $screenH
Open-T91-Remote "training_screen_a.html" "[T91_A]" $screenA_X $screenA_Y $screenA_W $screenA_H
Open-T91-Remote "training_screen_b.html" "[T91_B]" $screenB_X $screenB_Y $screenB_W $screenB_H

Write-Host ""
Write-Host "============================================" -ForegroundColor Green
Write-Host " T91 Remote Mode - Ready" -ForegroundColor Green
Write-Host " (No kiosk - windows can be moved/resized)" -ForegroundColor Green
Write-Host "============================================" -ForegroundColor Green

Wait-Process -Id $backendProc.Id -ErrorAction SilentlyContinue
Write-Host "Cleaning up..."
subst X: /D 2>$null
Write-Host "Done."
Stop-Process -Id $PID
