@echo off
rem Opens PowerShell with WXR's Appx cmdlets (Add-AppxPackage, Get-AppxPackage, Remove-AppxPackage) loaded.
powershell -NoExit -NoProfile -ExecutionPolicy Bypass -Command "Import-Module '%~dp0Modules\Appx'; Write-Host 'WXR Appx cmdlets loaded (environment: %~dp0..)'"
