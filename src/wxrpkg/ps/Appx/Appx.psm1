# WXR's Appx module: the Windows 10 cmdlet names over wxrpkg.exe (the WXR deployment engine).
# Lives in <root>\Layer\Modules\Appx; the engine is <root>\Layer\wxrpkg.exe.
$script:Engine = Join-Path $PSScriptRoot '..\..\wxrpkg.exe'

function Invoke-WxrEngine([string[]]$Arguments, [string]$Activity) {
    if (-not (Test-Path $script:Engine)) { throw "WXR deployment engine not found: $script:Engine" }
    $errors = New-Object System.Collections.Generic.List[string]
    $enc = [Console]::OutputEncoding
    try {
        [Console]::OutputEncoding = [Text.Encoding]::UTF8
        & $script:Engine @Arguments 2>&1 | ForEach-Object {
            if ($_ -is [System.Management.Automation.ErrorRecord]) {
                $m = "$_"; if ($m -match '^(ERROR|WARN): (.*)$') { if ($Matches[1] -eq 'WARN') { Write-Warning $Matches[2] } else { $errors.Add($Matches[2]) } } elseif ($m) { $errors.Add($m) }
            } else {
                Write-Verbose $_
                if ($Activity) { Write-Progress -Activity $Activity -Status "$_" }
            }
        }
    } finally { [Console]::OutputEncoding = $enc; if ($Activity) { Write-Progress -Activity $Activity -Completed } }
    if ($LASTEXITCODE -ne 0) {
        $msg = if ($errors.Count) { $errors -join [Environment]::NewLine } else { "wxrpkg failed with exit code $LASTEXITCODE" }
        throw "Deployment failed. $msg"
    }
    if ($Arguments[0] -in 'add', 'remove') { Update-WxrShell }
}

# Start menu, app names and icons follow every add/remove (wxr.exe sync: <root>\ProgramData\Shell + Start Menu\Programs\WXR)
function Update-WxrShell {
    $wxr = Join-Path $PSScriptRoot '..\..\wxr.exe'
    if (-not (Test-Path $wxr)) { Write-Verbose "wxr.exe not found ($wxr): Start menu not updated"; return }
    $p = Start-Process -FilePath $wxr -ArgumentList 'sync' -PassThru -Wait
    if ($p.ExitCode -ne 0) { Write-Warning "Start menu update failed (wxr sync exit code $($p.ExitCode))" }
}

function Add-AppxPackage {
    <# .SYNOPSIS Installs an app package (.appx, .msix, .appxbundle, .msixbundle) into the WXR environment. #>
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true, Position = 0, ValueFromPipeline = $true, ValueFromPipelineByPropertyName = $true)]
        [Alias('PSPath')] [string]$Path,
        [string[]]$DependencyPath,
        [switch]$Register,                      # -Register -Path <AppxManifest.xml>: the package folder is copied into WindowsApps
        [switch]$ForceUpdateFromAnyVersion,
        [switch]$ForceApplicationShutdown,      # accepted for compatibility (nothing to shut down yet)
        [switch]$ForceTargetApplicationShutdown,
        [switch]$DisableDevelopmentMode,        # accepted for compatibility
        [switch]$SkipOSVersionCheck             # WXR only: install packages that ask for a newer Windows 10 than the environment provides
    )
    process {
        $full = (Resolve-Path -LiteralPath $Path -ErrorAction Stop).ProviderPath
        if ($Register -and (Test-Path -LiteralPath $full -PathType Leaf) -and ((Split-Path $full -Leaf) -ne 'AppxManifest.xml')) { throw "-Register expects an AppxManifest.xml (or its folder): $full" }
        $a = @('add')
        foreach ($d in $DependencyPath) { $a += '--dependency'; $a += (Resolve-Path -LiteralPath $d -ErrorAction Stop).ProviderPath }   # a package or a folder of packages
        $a += $full
        if ($ForceUpdateFromAnyVersion) { $a += '--force-update' }
        if ($SkipOSVersionCheck) { $a += '--any-os' }
        Invoke-WxrEngine $a 'Deployment operation progress'
    }
}

function Get-AppxPackage {
    <# .SYNOPSIS Lists the app packages installed in the WXR environment. #>
    [CmdletBinding()]
    param(
        [Parameter(Position = 0)] [string]$Name = '*',
        [Parameter(Position = 1)] [string]$Publisher = '*',
        [switch]$AllUsers,
        [ValidateSet('Main', 'Framework', 'Resource', 'Bundle', 'Optional', 'All', 'None')] [string[]]$PackageTypeFilter = @('Main', 'Framework')
    )
    $types = if ($PackageTypeFilter -contains 'All') { 'all' } else { ($PackageTypeFilter | ForEach-Object { $_.ToLower() }) -join ',' }
    $a = @('list', '--json', '--type', $types, '--name', $Name, '--publisher', $Publisher)
    if ($AllUsers) { $a += '--all-users' }
    $enc = [Console]::OutputEncoding
    try { [Console]::OutputEncoding = [Text.Encoding]::UTF8; $json = (& $script:Engine @a) -join "`n" } finally { [Console]::OutputEncoding = $enc }
    if ($LASTEXITCODE -ne 0) { throw "Get-AppxPackage failed (wxrpkg exit code $LASTEXITCODE)" }
    $arch = @{ x64 = 'X64'; x86 = 'X86'; neutral = 'Neutral'; arm = 'Arm'; arm64 = 'Arm64' }
    $list = ConvertFrom-Json $json          # PS 5.1 returns a JSON array as one object: foreach enumerates it
    foreach ($p in $list) {
        if (-not $p) { continue }
        $o = [ordered]@{
            Name = $p.Name; Publisher = $p.Publisher; Architecture = $arch[$p.Architecture]; ResourceId = $p.ResourceId; Version = $p.Version
            PackageFullName = $p.PackageFullName; InstallLocation = $p.InstallLocation; IsFramework = $p.IsFramework; PackageFamilyName = $p.PackageFamilyName
            PublisherId = $p.PublisherId; IsResourcePackage = $p.IsResourcePackage; IsBundle = $false; IsDevelopmentMode = $false; NonRemovable = $false
            Dependencies = @($p.Dependencies); IsPartiallyStaged = $false; SignatureKind = $p.SignatureKind; Status = $p.Status
        }
        if ($AllUsers) { $o.PackageUserInformation = @($p.Users -split ';') }
        $obj = [pscustomobject]$o
        $obj.PSObject.TypeNames.Insert(0, 'WXR.AppxPackage')
        $obj
    }
}

function Remove-AppxPackage {
    <# .SYNOPSIS Removes an app package from the WXR environment (for the current user). #>
    [CmdletBinding(SupportsShouldProcess = $true)]
    param(
        [Parameter(Mandatory = $true, Position = 0, ValueFromPipeline = $true, ValueFromPipelineByPropertyName = $true)]
        [Alias('PackageFullName')] [string]$Package,
        [switch]$PreserveApplicationData,
        [switch]$AllUsers
    )
    process {
        if ($AllUsers) { throw '-AllUsers is not supported by WXR yet' }
        if (-not $PSCmdlet.ShouldProcess($Package, 'Remove package')) { return }
        $a = @('remove', $Package); if ($PreserveApplicationData) { $a += '--preserve-data' }
        Invoke-WxrEngine $a 'Deployment operation progress'
    }
}

Export-ModuleMember -Function Add-AppxPackage, Get-AppxPackage, Remove-AppxPackage
