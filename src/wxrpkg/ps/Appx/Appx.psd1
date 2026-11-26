@{
    RootModule        = 'Appx.psm1'
    ModuleVersion     = '0.1'
    GUID              = '6f5a3a2e-1c55-4d1e-9a7c-7a0e9e4b2c11'
    Author            = 'WXR'
    Description       = 'Add-AppxPackage, Get-AppxPackage and Remove-AppxPackage for a WXR environment (Windows 10 app packages on Windows 7).'
    PowerShellVersion = '3.0'
    FunctionsToExport = @('Add-AppxPackage', 'Get-AppxPackage', 'Remove-AppxPackage')
    CmdletsToExport   = @()
    AliasesToExport   = @()
}
