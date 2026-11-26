# deploy.ps1 - copies the built runtime (out\) into <root>\Layer. Usage: .\deploy.ps1 [-Root C:\WXR] [-Only sys,dcomp,rtstub,launcher,ordmap]
#   Layer\wxrsys.dll        loader layer + Win8/10 API implementations
#   Layer\wxrstub.dll     generated WinRT stand-in classes
#   Layer\override\dcomp.dll our DirectComposition; WXR modules importing dcomp.dll bind to it
#   Layer\ordmap\*.txt       ordinal -> name maps for ordinal imports
#   Layer\wxr.exe, Layer\wxrsvc.exe  launcher and broker (enable VxKex once for Layer\wxrsvc.exe; apps inherit it)
# wxrsetup install-layer replaces Layer\ as a whole; run this again after it.
# Refuses while anything from the root runs (apps and the broker hold the DLLs); an idle broker is stopped first.
param([string]$Root = 'C:\WXR', [string[]]$Only = @('sys','dcomp','rtstub','launcher','ordmap'))
$ErrorActionPreference = 'Stop'
$W = $PSScriptRoot; $out = "$W\out"; $L = "$Root\Layer"
if (-not (Test-Path "$Root\environment.ini")) { throw "$Root is not a WXR root" }
$Only = $Only | ForEach-Object { $_ -split ',' } | Where-Object { $_ }
$busy = Get-Process | Where-Object { $_.Path -like "$Root\*" }
if ($busy -and -not ($busy | Where-Object { $_.Path -like "$Root\WindowsApps\*" }) -and (Test-Path "$L\wxrsvc.exe")) {
  & "$L\wxrsvc.exe" --wxr $Root stop | Out-Null; Start-Sleep 2; $busy = Get-Process | Where-Object { $_.Path -like "$Root\*" } }
if ($busy) { "WXR processes running - not deploying:"; $busy | Select-Object Name, Id; exit 1 }
New-Item -ItemType Directory "$L\override", "$L\ordmap" -Force | Out-Null
if ($Only -contains 'sys')      { Copy-Item "$out\sysdll\wxrsys.dll" $L -Force; Remove-Item "$L\wxrsys.log" -ErrorAction SilentlyContinue }
if ($Only -contains 'rtstub')   { Copy-Item "$out\wxrstub.dll" $L -Force }
if ($Only -contains 'dcomp')    { Copy-Item "$out\dcomp\dcomp.dll" "$L\override\dcomp.dll" -Force }
if ($Only -contains 'launcher') { Copy-Item "$out\wxr.exe", "$out\wxrsvc.exe", "$out\wxruninstall.exe" $L -Force }
if ($Only -contains 'ordmap')   { Copy-Item "$W\data\ordmap\*.txt" "$L\ordmap" -Force }
"deployed ($($Only -join ', ')) to $L"
