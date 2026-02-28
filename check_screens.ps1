Add-Type -AssemblyName System.Windows.Forms
$screens = [System.Windows.Forms.Screen]::AllScreens
foreach ($s in $screens) {
    $tag = if ($s.Primary) {"[PRIMARY]"} else {""}
    Write-Host "X=$($s.Bounds.X) Y=$($s.Bounds.Y) W=$($s.Bounds.Width) H=$($s.Bounds.Height) $tag"
}
