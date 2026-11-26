# build.ps1 - builds WXR with the MinGW-w64 toolchain in deps\toolchain. Output goes to out\.
# Usage: .\build.ps1 [target ...]     targets: layer launcher broker dcomp rtstub wxrpkg wxrsetup msa tools   (default: all of them)
#                                     plus installer (needs VS 2019) and package (out\package: what users download)
#   out\sysdll\wxrsys.dll   loader layer + Win8/10 API implementations (kept apart from the host tools: forwarders would find it first)
#   out\wxrrun.exe          debug launcher            out\wxr.exe        launcher (activate/sync)
#   out\wxruninstall.exe    "Uninstall apps" (Start menu, WXR > Tools)
#   out\wxrsvc.exe          shared broker             out\wxrstub.dll generated WinRT stand-in classes
#   out\dcomp\dcomp.dll      our DirectComposition + Windows.UI.Composition
#   out\layer\               wxrpkg.exe + Appx module + data + licences, laid out like <root>\Layer
#   out\wxrsetup\            wxrsetup.exe + libwim + data + licences
#   out\msa\wxrsignin.exe    WXR Sign-in (the optional "Microsoft account sign-in" component; src\msa)
#   out\tools\               native debug tools (+ dbghelp.dll from deps)
# Prints "build ok" at the end; anything else means the build failed (the old binaries in out\ are then stale).
param([Parameter(ValueFromRemainingArguments = $true)][string[]]$Targets)   # every name on the command line (with -File, a plain [string[]] takes only the first)
if (-not $Targets) { $Targets = @('layer','launcher','broker','dcomp','rtstub','wxrpkg','wxrsetup','msa','tools') }
$ErrorActionPreference = 'Stop'
$W = $PSScriptRoot; $src = "$W\src"; $out = "$W\out"; $deps = "$W\deps"
$tc = "$deps\toolchain\x\mingw64\bin"; $wim = "$deps\toolchain\wimlib\bin"
if (-not (Test-Path "$tc\gcc.exe")) { throw "toolchain missing: $tc (see README.md, 'Dependencies')" }
$env:PATH = "$tc;" + $env:PATH
$cc = 'x86_64-w64-mingw32-gcc'
function Run([string[]]$a) {   # warnings on stderr must not abort (Windows PowerShell turns native stderr into errors); the exit code decides
  Write-Host "gcc $($a -join ' ')"; $ErrorActionPreference = 'Continue'; & $cc @a 2>&1 | ForEach-Object { "$_" }; if ($LASTEXITCODE) { throw "build failed" } }
function New-Dirs([string[]]$d) { New-Item -ItemType Directory -Force $d | Out-Null }
$Targets = $Targets | ForEach-Object { $_ -split ',' } | Where-Object { $_ }
# "retail": the runtime (layer, dcomp, rtstub, broker) is built with -DWXR_RETAIL = no logging at all (no wxrsys.log,
# wxr_activation.log, wxrsvc.log, no debug output). With no other target named, everything is built.
$RetailFlags = @(); if ($Targets -contains 'retail') { $RetailFlags = @('-DWXR_RETAIL'); $Targets = @($Targets | Where-Object { $_ -ne 'retail' })
  if (-not $Targets) { $Targets = @('layer','launcher','broker','dcomp','rtstub','wxrpkg','wxrsetup','msa','tools') }; Write-Host 'retail build: no logging' }
New-Dirs $out

if ($Targets -contains 'layer') {
  New-Dirs "$out\sysdll"
  Run (@('-shared','-O2','-w') + $RetailFlags + @('-o',"$out\sysdll\wxrsys.dll","$src\layer\wxrsys.c","$src\layer\wxrsys.def",'-lkernel32','-lole32','-loleaut32','-lgdi32','-luser32','-lcomctl32'))
}
if ($Targets -contains 'launcher') {
  Run @('-O2','-static','-w','-o',"$out\wxrrun.exe","$src\launcher\wxrrun.c")
  Run @('-O2','-static','-w','-mwindows','-o',"$out\wxr.exe","$src\launcher\wxr.c",'-lole32','-luuid','-lshell32')
  Run @('-O2','-static','-Wall','-municode','-o',"$out\wxruninstall.exe","$src\launcher\wxruninstall.c",'-lpsapi')
}
if ($Targets -contains 'broker') {
  Run (@('-O2','-w') + $RetailFlags + @('-o',"$out\wxrsvc.exe","$src\broker\wxrsvc.c",'-ldbghelp'))
}
if ($Targets -contains 'dcomp') {
  $c = "$src\dcomp"; $o = "$W\obj\dcomp"; New-Dirs $o, "$out\dcomp"
  Push-Location $c
  try {
    python -I tools/gen_dcomp_tables.py gen | Out-Null; if ($LASTEXITCODE) { throw 'gen_dcomp_tables failed' }
    & dlltool -d src/winrt_string.def -l "$o\libwinrt_string_api.a"; if ($LASTEXITCODE) { throw 'dlltool failed' }
    $FL = @('-O2','-w','-D_WIN32_WINNT=0x0601','-DDCOMP_BUILD','-Igen','-Isrc') + $RetailFlags
    function CC([string[]]$a) { & $cc @FL @a; if ($LASTEXITCODE) { throw "compile failed: $a" } }
    Remove-Item "$o\*.o" -ErrorAction SilentlyContinue
    CC @('-DDllGetActivationFactory=comp_engine_GetFactory','-c','src/comp_core.c','-o',"$o/comp_core.o")
    foreach ($f in 'comp_visual','comp_brush2','comp_effect','comp_anim','comp_shapes','comp_batch','comp_expr','comp_eval','comp_light','comp_interact','dcomp_obj','dcomp_render','dcomp_surface','dcomp_visual','dcomp_textproxy','dcomp_manip','dcomp_main') { CC @('-c',"src/$f.c",'-o',"$o/$f.o") }
    foreach ($f in 'dcomp_tables','dcomp_iids') { CC @('-c',"gen/$f.c",'-o',"$o/$f.o") }
    # generated stubs only for CI_ methods no hand-written source defines: a method implemented in src\ loses its stub here
    $defs = & nm (Get-ChildItem "$o\comp_*.o","$o\dcomp_*.o" | Where-Object { $_.Name -notin 'comp_gen.o','comp_stubs.o' } | ForEach-Object FullName) |
      ForEach-Object { $p = $_ -split '\s+'; if ($p.Count -ge 3 -and $p[1] -eq 'T' -and $p[2] -like 'CI_*') { $p[2] } } | Sort-Object -Unique
    [IO.File]::WriteAllLines("$o\defined.txt", [string[]]$defs)
    python -I tools/strip_stubs.py gen/comp_stubs.c "$o\defined.txt"; if ($LASTEXITCODE) { throw 'strip_stubs failed' }
    CC @('-c','gen/comp_gen.c','-o',"$o/comp_gen.o"); CC @('-c','gen/comp_stubs.c','-o',"$o/comp_stubs.o")
    & $cc -shared -o "$out\dcomp\dcomp.dll" src/dcomp.def (Get-ChildItem "$o\*.o" | ForEach-Object FullName) "-L$o" -lwinrt_string_api -ld3d11 -ldxgi -ld2d1 -ldwrite -lwindowscodecs -lole32 -luuid -luser32 -lgdi32 -lkernel32 -static-libgcc
    if ($LASTEXITCODE) { throw 'dcomp link failed' }
  } finally { Pop-Location }
}
if ($Targets -contains 'rtstub') {
  $r = "$src\rtstub"; $g = "$W\obj\rtstub"; New-Dirs $g
  python -I "$r\tools\gen_rtstub.py" $g "$r\classes.txt"; if ($LASTEXITCODE) { throw 'gen_rtstub failed' }
  Run (@('-O1','-shared','-w') + $RetailFlags + @('-I',"$r\src",'-o',"$out\wxrstub.dll") + (Get-ChildItem "$r\src\*.c" | ForEach-Object FullName) + @("$g\rt_gen.c",'-static-libgcc','-lshell32','-lole32','-luuid','-lwinhttp','-lcrypt32'))
}
if ($Targets -contains 'wxrpkg') {
  $p = "$src\wxrpkg"; $L = "$out\layer"; $mz = "$p\third_party\miniz\src"
  New-Dirs $L, "$L\Modules\Appx", "$L\licenses\miniz", "$L\licenses\storelib", "$L\data"
  Run @('-O2','-Wall','-Wno-unused-function','-municode','-static','-I',$mz,'-I',"$p\src",'-o',"$L\wxrpkg.exe","$p\src\wxrpkg.c","$p\src\xml.c","$mz\miniz.c",'-lbcrypt','-lshlwapi','-lxmllite','-lole32','-luuid','-lgdi32','-lwinhttp','-lcrypt32','-lversion')
  Copy-Item "$p\ps\Appx\*" "$L\Modules\Appx" -Force
  Copy-Item "$p\ps\wxr-powershell.cmd" $L -Force
  Copy-Item "$mz\LICENSE" "$L\licenses\miniz" -Force
  Copy-Item "$p\third_party\storelib\WUIDRequest.xml" "$L\data\wu_sync_request.xml" -Force   # wxrpkg store: the SyncUpdates request (StoreLib, MPL-2.0)
  Copy-Item "$p\third_party\storelib\LICENSE", "$p\third_party\storelib\README.txt" "$L\licenses\storelib" -Force
  "WXR layer 0.2" | Set-Content "$L\layer.txt" -Encoding Ascii
}
if ($Targets -contains 'wxrsetup') {
  $s = "$src\wxrsetup"; $o = "$out\wxrsetup"; New-Dirs $o, "$o\data", "$o\licenses\wimlib"
  Run @('-O2','-Wall','-municode','-static','-I',"$wim\devel",'-o',"$o\wxrsetup.exe","$s\src\wxrsetup.c","$s\src\udf.c","$s\src\hive.c",'-ladvapi32','-lversion','-lshell32','-lole32','-luuid')
  Copy-Item "$wim\libwim-15.dll" $o -Force
  Copy-Item "$wim\COPYING.txt", "$wim\COPYING.GPLv3.txt", "$wim\COPYING.LGPLv3.txt", "$wim\COPYING.libdivsufsort-lite.txt" "$o\licenses\wimlib" -Force
  Copy-Item "$s\data\*.txt" "$o\data" -Force
}
if ($Targets -contains 'msa') {
  New-Dirs "$out\msa"
  Run @('-O2','-Wall','-static','-o',"$out\msa\wxrsignin.exe","$src\msa\wxrsignin.c",'-lwinhttp','-lcrypt32','-lshell32')
}
if ($Targets -contains 'tools') {
  $t = "$out\tools"; New-Dirs $t
  foreach ($n in 'exprcheck') { Run @('-O2','-w','-o',"$t\$n.exe","$W\tools\native\$n.c") }
  foreach ($n in 'symname','symq') { Run @('-O2','-w','-o',"$t\$n.exe","$W\tools\native\$n.c",'-ldbghelp') }
  if (Test-Path "$deps\dbghelp.dll") { Copy-Item "$deps\dbghelp.dll" $t -Force }   # Win10 dbghelp: Win7's 6.1 misreads newer PDBs
}
if ($Targets -contains 'installer') {
  # the WinForms setup program (.NET 3.5); built with Visual Studio 2019's MSBuild (the .NET 4 MSBuild lacks the 3.5 resource tools)
  $vsw = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
  $msb = if (Test-Path $vsw) { & $vsw -latest -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1 }
  if (-not $msb) { throw 'installer: Visual Studio 2019 (MSBuild) not found' }
  & $msb "$src\installer\WxrInstaller\WxrInstaller.csproj" /nologo /v:m /p:Configuration=Release
  if ($LASTEXITCODE) { throw 'installer build failed' }
}
if ($Targets -contains 'package') {
  # out\package = what a user downloads: the setup program, wxrsetup + its data, and layer\ = the complete <root>\Layer
  # (needs the other targets built first: .\build.ps1 then .\build.ps1 installer package)
  $pk = "$out\package"; $pl = "$pk\layer"
  if (Test-Path $pk) { Remove-Item $pk -Recurse -Force }
  New-Dirs $pk, $pl, "$pl\override", "$pl\ordmap"
  Copy-Item "$src\installer\WxrInstaller\bin\Release\WxrInstaller.exe", "$src\installer\WxrInstaller\bin\Release\WxrInstaller.exe.config" $pk
  Copy-Item "$out\wxrsetup\*" $pk -Recurse
  New-Dirs "$pk\msa"; Copy-Item "$out\msa\wxrsignin.exe" "$pk\msa"   # the optional sign-in component: wxrsetup components copies it into Layer
  Copy-Item "$out\layer\*" $pl -Recurse                                     # wxrpkg, Appx module, data, licences
  Copy-Item "$out\sysdll\wxrsys.dll", "$out\wxrstub.dll", "$out\wxr.exe", "$out\wxrsvc.exe", "$out\wxruninstall.exe" $pl
  Copy-Item "$out\dcomp\dcomp.dll" "$pl\override"
  Copy-Item "$W\data\ordmap\*.txt" "$pl\ordmap"
  $n = (Get-ChildItem $pk -Recurse -File).Count; $mb = [math]::Round((Get-ChildItem $pk -Recurse -File | Measure-Object Length -Sum).Sum / 1MB, 1)
  Write-Host "package: $pk ($n files, $mb MB)"
}
Write-Host 'build ok'
