Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
using System.Text;
using System.Collections.Generic;
public class WinCap {
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr l);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int c);
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  public struct RECT { public int Left, Top, Right, Bottom; }
  public static List<IntPtr> FindByTitle(string part, uint targetPid) {
    List<IntPtr> res = new List<IntPtr>();
    EnumWindows((h, l) => {
      if (!IsWindowVisible(h)) return true;
      uint pid; GetWindowThreadProcessId(h, out pid);
      if (pid != targetPid) return true;
      StringBuilder sb = new StringBuilder(256);
      GetWindowText(h, sb, 256);
      if (sb.ToString().Contains(part)) res.Add(h);
      return true;
    }, IntPtr.Zero);
    return res;
  }
}
"@
$proc = Get-Process WatermarkInjectionLauncher -ErrorAction SilentlyContinue
if (-not $proc) { Write-Host "process not found"; exit 1 }
$wins = [WinCap]::FindByTitle("Watermark", $proc.Id)
if ($wins.Count -eq 0) { $wins = [WinCap]::FindByTitle("", $proc.Id) }
if ($wins.Count -eq 0) { Write-Host "no window"; exit 1 }
$h = $wins[0]
[WinCap]::ShowWindow($h, 9) | Out-Null
[WinCap]::SetForegroundWindow($h) | Out-Null
Start-Sleep -Milliseconds 800
$r = New-Object WinCap+RECT
[WinCap]::GetWindowRect($h, [ref]$r) | Out-Null
$w = $r.Right - $r.Left; $hgt = $r.Bottom - $r.Top
$bmp = New-Object System.Drawing.Bitmap $w, $hgt
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen($r.Left, $r.Top, 0, 0, $bmp.Size)
$bmp.Save("D:\watermark injecting Version\_analysis\launcher_check.png")
Write-Host "saved $w x $hgt from pid $($proc.Id)"
