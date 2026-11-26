# regen_comp.ps1 - regenerate the composition ABI layer after adding classes to src\dcomp\tools\gen_comp.py CLASSES, keeping the hand edits.
#   comp_abi.h / comp_gen.h / comp_gen.c are replaced by the regenerated ones;
#   comp_stubs.c keeps every shipped function (hand edits) and only gains stubs for methods it did not have (merge_stubs.py).
# build.ps1 dcomp afterwards removes the stubs of methods the hand-written sources define (strip_stubs.py).
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$env:PATH = "$repo\deps\toolchain\x\mingw64\bin;" + $env:PATH
$env:WXR_WINMD_ROOT = "$repo\deps\winmd"
$c = "$repo\src\dcomp"; $tmp = "$c\gen_regen"
Remove-Item $tmp -Recurse -Force -ErrorAction SilentlyContinue; New-Item -ItemType Directory $tmp | Out-Null
Push-Location "$c\tools"
python gen_comp.py $tmp; if ($LASTEXITCODE) { Pop-Location; throw 'gen_comp failed' }
Pop-Location
python "$PSScriptRoot\merge_stubs.py" "$c\gen\comp_stubs.c" "$tmp\comp_stubs.c" "$tmp\comp_stubs.merged.c"; if ($LASTEXITCODE) { throw 'merge failed' }
Copy-Item "$tmp\comp_abi.h", "$tmp\comp_gen.h", "$tmp\comp_gen.c" "$c\gen" -Force
Copy-Item "$tmp\comp_stubs.merged.c" "$c\gen\comp_stubs.c" -Force
Remove-Item $tmp -Recurse -Force
'regenerated gen\ (stubs merged)'
