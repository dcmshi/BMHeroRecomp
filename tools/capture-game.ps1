# Capture the BMHeroRecompiled game window to a PNG so Claude can read/analyze it.
# Uses PrintWindow(PW_RENDERFULLCONTENT) which grabs the window's own content
# even when it's occluded / not focused (works for this RT64/Vulkan window).
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class PW {
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
}
"@
$out = Join-Path $PSScriptRoot "game.png"
$p = Get-Process BMHeroRecompiled -ErrorAction SilentlyContinue | Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1
if (-not $p) { Write-Output "GAME NOT RUNNING"; exit 1 }
$h = $p.MainWindowHandle
$r = New-Object PW+RECT; [PW]::GetClientRect($h, [ref]$r) | Out-Null
$w = $r.Right - $r.Left; $ht = $r.Bottom - $r.Top
if ($w -le 0 -or $ht -le 0) { Write-Output "BAD RECT ($w x $ht)"; exit 1 }
$bmp = New-Object System.Drawing.Bitmap $w, $ht
$g = [System.Drawing.Graphics]::FromImage($bmp); $hdc = $g.GetHdc()
$ok = [PW]::PrintWindow($h, $hdc, 2)   # PW_RENDERFULLCONTENT
$g.ReleaseHdc($hdc); $g.Dispose()
$bmp.Save($out, [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
Write-Output "SAVED $out (${w}x${ht}) ok=$ok from PID $($p.Id)"
