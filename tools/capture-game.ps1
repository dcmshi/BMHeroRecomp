# Capture the BMHeroRecompiled game window to a PNG so Claude can read/analyze it.
# Uses PrintWindow(PW_RENDERFULLCONTENT) which grabs the window's own content
# even when it's occluded / not focused (works for this RT64/D3D12 window).
#
# ============================ READ THIS ====================================
# THE DPI CALL BELOW IS LOAD-BEARING. Without it this script silently captured
# only the TOP-LEFT QUARTER of the frame, and did so for weeks.
#
# The window's backbuffer is 1600x900, but on a high-DPI display Windows reports
# GetClientRect to a NON-DPI-AWARE process in LOGICAL pixels - 800x450. We then
# allocated an 800x450 bitmap and PrintWindow blitted the top-left 800x450 of the
# real surface into it, unscaled. The result looked plausible (real game content,
# correctly lit, animating, and it changed when the camera changed), which is
# exactly what made it dangerous: it was a believable lie.
#
# The cost: it made the A1.5 fixed camera look broken. The arena appeared shoved
# into the bottom-right and to shrink as the camera pulled back - because we were
# only ever seeing the top-left quarter, so the centred arena drifted out of the
# captured region. Three separate root causes were hypothesised and tested
# against these screenshots before a RenderDoc capture of the same frame showed
# the camera had been correct the whole time (integration notes 8.17).
#
# If a screenshot ever disagrees with a measurement again, capture the frame with
# RenderDoc and compare before trusting the screenshot.
# ===========================================================================
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class PW {
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
}
"@
# Must happen BEFORE GetClientRect, and before any GDI work, or the numbers come
# back virtualised and we crop the frame without any error.
[PW]::SetProcessDPIAware() | Out-Null

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
