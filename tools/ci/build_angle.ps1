# Builds ANGLE (OpenGL ES on Direct3D 11) for UWP x64: libEGL.dll and libGLESv2.dll, with their import
# libraries and headers, for the game's UWP graphics backend (docs/UWP-GRAPHICS.md).
# Windows only. Follows ANGLE's doc/DevSetup.md: depot_tools, `fetch angle`, `gn gen` with
# target_os = "winuwp", `autoninja`. Always exits 0 so the logs are published; check angle-uwp\RESULT.txt.
param(
  [string]$Ref = 'main',      # an ANGLE branch, tag or commit
  [int]$TimeoutMin = 52       # a hosted job stops at 60 minutes
)
$ErrorActionPreference = 'Continue'
$deadline = (Get-Date).AddMinutes($TimeoutMin)
$out = Join-Path $PWD 'angle-uwp'
New-Item -ItemType Directory -Force $out | Out-Null
$result = @()
function Say($m) { Write-Host "== $m"; $script:result += $m }

# The runner's Visual Studio stands in for Google's toolchain.
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs = & $vswhere -latest -products '*' -property installationPath
$env:DEPOT_TOOLS_WIN_TOOLCHAIN = '0'
$env:GYP_MSVS_OVERRIDE_PATH = $vs
$env:GYP_MSVS_VERSION = '2022'
$env:vs2022_install = $vs

git config --global core.autocrlf false
git config --global core.longpaths true
git config --global core.filemode false

# The runner can carry a Windows SDK folder that is not complete (no um\Windows.h); vcvarsall picks the
# newest version it finds and Chromium's toolchain script then stops on the missing include folder.
# The runner is thrown away afterwards, so drop the incomplete ones. sdk.log lists what was there.
$kits = 'C:\Program Files (x86)\Windows Kits\10'
$sdkLog = @()
foreach ($d in Get-ChildItem "$kits\Include" -Directory -ErrorAction SilentlyContinue) {
  $ok = Test-Path "$($d.FullName)\um\Windows.h"
  $sdkLog += "$($d.Name): um\Windows.h $(if ($ok) { 'present' } else { 'MISSING, removed' })"
  if (-not $ok) {
    foreach ($sub in 'Include', 'Lib', 'bin', 'Source') { Remove-Item "$kits\$sub\$($d.Name)" -Recurse -Force -ErrorAction SilentlyContinue }
  }
}
$sdkLog | Tee-Object "$out\sdk.log" | Out-Host

$tools = 'D:\depot_tools'
$src = 'D:\angle'
if (-not (Test-Path $tools)) {
  Say 'cloning depot_tools'
  git clone --depth 1 https://chromium.googlesource.com/chromium/tools/depot_tools.git $tools 2>&1 | Out-Host
}
$env:PATH = "$tools;$env:PATH"

if (-not (Test-Path "$src\angle")) {
  Say 'fetch angle'
  New-Item -ItemType Directory -Force $src | Out-Null
  Set-Location $src
  fetch angle 2>&1 | Tee-Object "$out\fetch.log" | Select-Object -Last 15 | Out-Host
}
Set-Location "$src\angle"
if ($Ref -ne 'main') {
  Say "checking out $Ref"
  git fetch origin $Ref 2>&1 | Out-Host
  git checkout FETCH_HEAD 2>&1 | Out-Host
  gclient sync -D --force 2>&1 | Tee-Object "$out\sync.log" | Select-Object -Last 10 | Out-Host
}
$commit = git rev-parse HEAD
Say "ANGLE at $commit"

# UWP disables D3D9, desktop GL and Vulkan itself (gni/angle.gni); only the D3D11 backend remains.
New-Item -ItemType Directory -Force out\uwp | Out-Null
@'
target_os = "winuwp"
target_cpu = "x64"
is_debug = false
is_component_build = false
is_clang = false
symbol_level = 0
'@ | Set-Content -Encoding ascii out\uwp\args.gn
Say 'gn gen'
gn gen out\uwp 2>&1 | Tee-Object "$out\gn.log" | Out-Host
$gnOk = ($LASTEXITCODE -eq 0)

$built = $false
if ($gnOk) {
  Say 'autoninja libEGL libGLESv2'
  $left = [int]($deadline - (Get-Date)).TotalMilliseconds
  $p = Start-Process autoninja.bat -ArgumentList @('-C', 'out\uwp', 'libEGL', 'libGLESv2') -NoNewWindow -PassThru `
    -RedirectStandardOutput "$out\ninja.log" -RedirectStandardError "$out\ninja.err.log"
  if (-not $p.WaitForExit($left)) {
    Say 'timed out: the build is incomplete'
    taskkill /PID $p.Id /T /F | Out-Null
  }
  $built = (Test-Path out\uwp\libEGL.dll) -and (Test-Path out\uwp\libGLESv2.dll)
}

if ($built) {
  Say 'collecting the DLLs, import libraries and headers'
  New-Item -ItemType Directory -Force "$out\bin", "$out\lib", "$out\include" | Out-Null
  Copy-Item out\uwp\libEGL.dll, out\uwp\libGLESv2.dll "$out\bin\"
  Copy-Item out\uwp\libEGL.dll.lib, out\uwp\libGLESv2.dll.lib "$out\lib\" -ErrorAction SilentlyContinue
  foreach ($d in 'EGL', 'GLES2', 'GLES3', 'KHR') { Copy-Item "include\$d" "$out\include\$d" -Recurse -ErrorAction SilentlyContinue }
  # ANGLE's Direct3D backend compiles shaders with the D3D compiler, which a UWP app has to carry itself.
  $d3dc = Get-ChildItem 'C:\Program Files (x86)\Windows Kits\10\Redist\D3D\x64\d3dcompiler_47.dll' -ErrorAction SilentlyContinue | Select-Object -First 1
  if ($d3dc) { Copy-Item $d3dc.FullName "$out\bin\" } else { Say 'd3dcompiler_47.dll not found in the SDK redistributables' }
  Say 'ANGLE built'
} else {
  Say 'ANGLE was not built (see gn.log and ninja.log)'
}
"$commit`n$(Get-Content out\uwp\args.gn -Raw)" | Set-Content "$out\ANGLE_REF.txt"
$result | Set-Content "$out\RESULT.txt"
exit 0
