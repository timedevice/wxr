# appshot.ps1 -App calc|clock|media|store -Proc <process name> -Out <png> [-Wait s] [-Scroll] [-Click "x,y" [-After s]]
# Debug-runs an app (wxr_run.ps1), brings its window to the front and saves a snapshot of the window rectangle only (not the desktop).
# -Scroll: also wheels down over the page and saves <png minus .png>_scrolled.png 8 s later.
# Environment variables set by the caller (WXR_*) reach the app.
# -Click "x,y": left-clicks at that point (window pixels, i.e. snapshot pixels / 0.7) after the first snapshot and saves <png>_clicked.png -After s later.
param([string]$App = 'calc', [string]$Proc = 'CalculatorApp', [string]$Out, [int]$Wait = 30, [switch]$Scroll, [string]$Click = '', [int]$After = 10)
Add-Type -AssemblyName System.Drawing
Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @'
using System; using System.Runtime.InteropServices; using System.Diagnostics;
public static class WShot {
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
  delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc f, IntPtr l);
  [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int n);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int cx, int cy, uint flags);
  // SetForegroundWindow is refused when another program has the focus; topmost (without activation) always shows the window
  public static void Top(IntPtr h, bool on) { SetWindowPos(h, new IntPtr(on ? -1 : -2), 0, 0, 0, 0, 0x13); }
  [DllImport("user32.dll")] public static extern void mouse_event(uint f, uint dx, uint dy, int data, UIntPtr extra);
  // largest visible top-level window of a process with this name
  public static IntPtr Find(string procName) {
    IntPtr best = IntPtr.Zero; long area = 0;
    foreach (Process p in Process.GetProcessesByName(procName)) { uint want = (uint)p.Id;
      EnumWindows(delegate(IntPtr h, IntPtr l) { uint pid; GetWindowThreadProcessId(h, out pid);
        if (pid == want && IsWindowVisible(h)) { RECT r; GetWindowRect(h, out r); long a = (long)(r.R - r.L) * (r.B - r.T); if (a > area) { area = a; best = h; } }
        return true; }, IntPtr.Zero); }
    return best; }
}
'@
function shot($hw, $name) {
  $r = New-Object WShot+RECT; [WShot]::GetWindowRect($hw, [ref]$r) | Out-Null; $w = $r.R - $r.L; $h = $r.B - $r.T; if ($w -le 0 -or $h -le 0) { "bad window rect"; return }
  $bmp = New-Object System.Drawing.Bitmap $w, $h; $g = [System.Drawing.Graphics]::FromImage($bmp)
  $g.CopyFromScreen($r.L, $r.T, 0, 0, (New-Object System.Drawing.Size $w, $h))
  $s = New-Object System.Drawing.Bitmap $bmp, ([int]($w * 0.7)), ([int]($h * 0.7)); $s.Save($name, [System.Drawing.Imaging.ImageFormat]::Png); $g.Dispose(); $bmp.Dispose(); $s.Dispose(); "saved $name" }
$extra = if ($Scroll) { 22 } elseif ($Click) { $After + 12 } else { 8 }
$job = Start-Process powershell -ArgumentList '-NoProfile','-File',(Join-Path $PSScriptRoot 'wxr_run.ps1'),'-App',$App,'-Seconds',($Wait + $extra) -WindowStyle Hidden -PassThru
Start-Sleep $Wait
$hw = [WShot]::Find($Proc)
if ($hw -eq [IntPtr]::Zero) { "no $Proc window" } else {
  [WShot]::ShowWindow($hw, 9) | Out-Null; [WShot]::Top($hw, $true); [WShot]::SetForegroundWindow($hw) | Out-Null; Start-Sleep 2
  shot $hw $Out
  if ($Click) { $c = $Click -split ','; $r = New-Object WShot+RECT; [WShot]::GetWindowRect($hw, [ref]$r) | Out-Null
    [WShot]::SetCursorPos($r.L + [int]$c[0], $r.T + [int]$c[1]) | Out-Null; Start-Sleep -Milliseconds 400
    [WShot]::mouse_event(2, 0, 0, 0, [UIntPtr]::Zero); Start-Sleep -Milliseconds 80; [WShot]::mouse_event(4, 0, 0, 0, [UIntPtr]::Zero)
    "clicked $Click at " + (Get-Date -Format HH:mm:ss.fff); Start-Sleep $After
    if ([WShot]::Find($Proc) -eq [IntPtr]::Zero) { "$Proc window is gone after the click" } else { shot $hw ($Out -replace '\.png$', '_clicked.png') } }
  if (-not $Scroll) { [WShot]::Top($hw, $false) }
  if ($Scroll) {
    $r = New-Object WShot+RECT; [WShot]::GetWindowRect($hw, [ref]$r) | Out-Null
    [WShot]::SetCursorPos([int](($r.L + $r.R) / 2), [int](($r.T + $r.B) / 2)) | Out-Null; Start-Sleep -Milliseconds 300
    for ($i = 0; $i -lt 12; $i++) { [WShot]::mouse_event(0x0800, 0, 0, -120, [UIntPtr]::Zero); Start-Sleep -Milliseconds 150 }
    Start-Sleep 8; shot $hw ($Out -replace '\.png$', '_scrolled.png'); [WShot]::Top($hw, $false) } }
$job.WaitForExit(90000) | Out-Null

