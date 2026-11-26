# Runs an app installed in a WXR root under the debug launcher (wxrrun --wxr): Win10 files from the image, loader layer.
# Usage: wxr_run.ps1 [-App calc|clock|media] [-Seconds 14] [-Root C:\WXR] [-Interactive]
# Logs: logs\wxr_run.log (wxrrun), logs\wxr_sys.log (wxrsys + [ldr]).
# The registrar stand-in wxrsvc runs in WXR mode too (pristine CoreMessaging from the image, loader layer); log <root>\Logs\wxrsvc.log.
param([string]$App = 'calc', [int]$Seconds = 14, [string]$Root = 'C:\WXR', [switch]$Interactive)
$apps = @{ calc = @('Microsoft.WindowsCalculator_*_x64__8wekyb3d8bbwe', 'CalculatorApp', 'App.AppXwxr0calc0000000000000000000.mca', 'Calculator')
           clock = @('Microsoft.WindowsAlarms_*_x64__8wekyb3d8bbwe', 'Time', 'App.AppXwxr0clock000000000000000000.mca', 'Clock')
           media = @('Microsoft.ZuneMusic_*_x64__8wekyb3d8bbwe', 'Microsoft.Media.Player', 'App.AppXwxr0media000000000000000000.mca', 'Media Player')
           store = @('Microsoft.WindowsStore_*_x64__8wekyb3d8bbwe', 'WinStore.App', 'App.AppXwxr0store000000000000000000.mca', 'Microsoft Store') }
$repo = Split-Path $PSScriptRoot -Parent; $b = "$repo\out"; $w = "$repo\logs"; $a = $apps[$App]
New-Item -ItemType Directory $w -Force | Out-Null
$pkg = Get-ChildItem "$Root\WindowsApps" -Directory -Filter $a[0] | sort Name | select -Last 1
if (-not $pkg) { "no $($a[0]) in $Root\WindowsApps"; exit 1 }
$exe = Join-Path $pkg.FullName "$($a[1]).exe"
Get-Process wxrsvc, CalculatorApp, Time, Microsoft.Media.Player, WinStore.App, wxrrun -ErrorAction SilentlyContinue | Stop-Process -Force
Remove-Item "$Root\Layer\wxrsys.log", "$b\wxrrun.log" -Force -ErrorAction SilentlyContinue
$sv = Start-Process "$b\wxrsvc.exe" -ArgumentList '--wxr', $Root -WindowStyle Hidden -RedirectStandardOutput "$w\svc.txt" -RedirectStandardError "$w\svc_err.txt" -PassThru
Start-Sleep 4
# set only now: the broker must not inherit them, or every app it launches later (the Store's "Open") gets this app's title
$env:WXR_WINDOW_TITLE = $a[3]; $env:WXR_WINDOW_SIZE = @{ media = '1100x720' }[$App]
Set-Location $pkg.FullName
$p = Start-Process "$b\wxrrun.exe" -ArgumentList '--wxr', '--root', $Root, $exe, "-ServerName:$($a[2])" -PassThru -WindowStyle Hidden -RedirectStandardOutput "$w\run_out.txt" -RedirectStandardError "$w\run_err.txt"
$null = $p.Handle
if ($Interactive) { $p.WaitForExit() }
elseif (-not $p.WaitForExit($Seconds * 1000)) { "still running after ${Seconds}s"; Get-Process wxrrun, $a[1] -ErrorAction SilentlyContinue | % { taskkill /F /PID $_.Id 2>&1 | Out-Null }; Start-Sleep 2 }   # Stop-Process can hang on a debuggee
& "$b\wxrsvc.exe" --wxr $Root stop | Out-Null
for ($k = 0; $k -lt 10; $k++) { try { Copy-Item "$Root\Layer\wxrsys.log" "$w\wxr_sys.log" -Force -ErrorAction Stop; break } catch { Start-Sleep -Milliseconds 500 } }
for ($k = 0; $k -lt 10; $k++) { try { Copy-Item "$b\wxrrun.log" "$w\wxr_run.log" -Force -ErrorAction Stop; break } catch { Start-Sleep -Milliseconds 500 } }   # the runner may still hold it while it is being killed
Select-String -Path "$w\wxr_run.log" -Pattern '\[wxr\]|\[exc\]|app exited|CreateProcess' | % { $_.Line.Trim() } | select -First 25
"--- loader:"; Select-String -Path "$w\wxr_sys.log" -Pattern '\[ldr\] (WXR|start|THUNK|.*FAIL|.*NOT LOADABLE|WARNING|import name changed|no room|cannot)' | % { $_.Line -replace '^\[\d+\] ', '' } | select -First 40
