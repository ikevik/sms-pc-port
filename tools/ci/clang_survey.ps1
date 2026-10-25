# Finds out how far the game builds with LLVM clang targeting x86_64-pc-windows-msvc.
# Windows only. Configures with the repo's own CMake (graphics layer off), builds with
# `ninja -k 0` so every translation unit is tried, and summarises the errors.
# Always exits 0 so the logs and summary can be published; read summary.md for the result.
param(
  [string]$Build = 'build-clang',
  [int]$TimeoutMin = 50,
  [switch]$Uwp   # UWP: the store environment and libraries, no SDL2 (see SMS_UWP in CMakeLists.txt)
)
$ErrorActionPreference = 'Continue'
$deadline = (Get-Date).AddMinutes($TimeoutMin)
$out = Join-Path $PWD "$Build-survey"
New-Item -ItemType Directory -Force $out | Out-Null

function Say($m) { Write-Host "== $m" }

# The MSVC headers and libraries the msvc target compiles and links against.
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs = & $vswhere -latest -products '*' -property installationPath
if (-not $vs) { Write-Host 'Visual Studio not found'; exit 0 }
$vcArgs = if ($Uwp) { 'x64 uwp' } else { 'x64' }
cmd /c "`"$vs\VC\Auxiliary\Build\vcvarsall.bat`" $vcArgs >nul 2>&1 && set" | ForEach-Object {
  if ($_ -match '^([^=]+)=(.*)$') { Set-Item -Path "env:$($Matches[1])" -Value $Matches[2] }
}

# clang: the runner's, the one bundled with Visual Studio, or a fresh install.
if (-not (Get-Command clang++ -ErrorAction SilentlyContinue)) {
  foreach ($d in @("$env:ProgramFiles\LLVM\bin", "$vs\VC\Tools\Llvm\x64\bin")) {
    if (Test-Path "$d\clang++.exe") { $env:PATH = "$d;$env:PATH"; break }
  }
}
if (-not (Get-Command clang++ -ErrorAction SilentlyContinue)) {
  Say 'installing LLVM'
  choco install llvm -y --no-progress | Out-Host
  $env:PATH = "$env:ProgramFiles\LLVM\bin;$env:PATH"
}
if (-not (Get-Command ninja -ErrorAction SilentlyContinue)) {
  $n = "$vs\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja"
  if (Test-Path $n) { $env:PATH = "$n;$env:PATH" } else { choco install ninja -y --no-progress | Out-Host }
}
# Strawberry Perl's patch.exe (2.5.9) asserts on the decomp patches; drop it so Git's is used.
$env:PATH = (($env:PATH -split ';') | Where-Object { $_ -and $_ -notmatch 'Strawberry' }) -join ';'
# patch.exe, for decomp-patches/ (last, so MSVC's link.exe stays first on PATH)
$env:PATH += ";$env:ProgramFiles\Git\usr\bin"

(clang++ --version) + (cmake --version) + (python --version) | Tee-Object "$out\toolchain.txt" | Out-Host

# SDL2's headers and import library, the version build.sh pins (the survey only compiles and links;
# a UWP build needs SDL2's WinRT backend instead).
$sdlVer = '2.30.11'
$sdlDir = "$PWD\$Build-sdl"
try {
  if ($Uwp) { throw 'skipped for UWP' }
  Say "fetching SDL2 $sdlVer"
  Invoke-WebRequest "https://github.com/libsdl-org/SDL/releases/download/release-$sdlVer/SDL2-devel-$sdlVer-VC.zip" `
    -OutFile "$out\SDL2-devel.zip" -UseBasicParsing
  Expand-Archive "$out\SDL2-devel.zip" $sdlDir -Force
  Remove-Item "$out\SDL2-devel.zip"
} catch { Write-Host "SDL2 download failed: $_" }
$sdlCmake = "$sdlDir\SDL2-$sdlVer\cmake"

Say 'configure'
$cmakeArgs = @('-S', '.', '-B', $Build, '-G', 'Ninja',
  '-DCMAKE_C_COMPILER=clang', '-DCMAKE_CXX_COMPILER=clang++',
  '-DCMAKE_BUILD_TYPE=RelWithDebInfo', '-DSMS_ARCH=64', '-DSMS_WITH_GX=ON',
  '-DSMS_GAME_CXX_STD=gnu++14', '-DSMS_PLATFORM_CXX_STD=gnu++14')
if (Test-Path $sdlCmake) { $cmakeArgs += "-DSDL2_DIR=$sdlCmake" }
if ($Uwp) { $cmakeArgs += '-DSMS_UWP=ON' }
cmake @cmakeArgs *>&1 | Tee-Object "$out\configure.log" | Select-Object -Last 40 | Out-Host
$configured = ($LASTEXITCODE -eq 0)

function Run-Ninja([string[]]$targets, [string]$log) {
  $left = [int]($deadline - (Get-Date)).TotalMilliseconds
  if ($left -le 0) { Say "no time left for $log"; return }
  $ninjaArgs = @('-C', $Build, '-k', '0') + $targets
  $p = Start-Process ninja -ArgumentList $ninjaArgs -NoNewWindow -PassThru `
    -RedirectStandardOutput $log -RedirectStandardError "$log.err"
  if (-not $p.WaitForExit($left)) {
    Say "timed out, stopping $($targets -join ' ')"
    taskkill /PID $p.Id /T /F | Out-Null
  }
  if (Test-Path "$log.err") { Get-Content "$log.err" | Add-Content $log; Remove-Item "$log.err" }
}

if ($configured) {
  Say 'game sources (decomp, port menus)'
  Run-Ninja @('sms_game') "$out\build-game.log"
  Say 'platform layer and executable'
  Run-Ninja @('sms') "$out\build-sms.log"
} else {
  Say 'configure failed; the summary will show why'
}

# A linked sms.exe goes in the artifact (bin/) with SDL2.dll, for trying it on a Windows PC:
#   sms.exe path\to\GMSE01.iso
if (Test-Path "$Build\sms.exe") {
  New-Item -ItemType Directory -Force "$out\bin" | Out-Null
  Copy-Item "$Build\sms.exe" "$out\bin\"
  $dll = "$sdlDir\SDL2-$sdlVer\lib\x64\SDL2.dll"
  if (Test-Path $dll) { Copy-Item $dll "$out\bin\" }
  Say "sms.exe linked: $((Get-Item "$Build\sms.exe").Length / 1MB) MB"
}

# When the link fails, show how the objects define the symbols involved (dumpbin ships with MSVC).
if ($configured -and -not (Test-Path "$Build\sms.exe")) {
  Say 'link diagnostics'
  $diag = foreach ($o in @(
      'CMakeFiles\sms_game.dir\ext\multiplayer\game\mp_game.cpp.obj',
      'CMakeFiles\sms_game.dir\ext\multiplayer\game\mp_server.cpp.obj',
      'CMakeFiles\sms_game.dir\ext\coop\game\coop.cpp.obj',
      'platform\gx\CMakeFiles\sms_gx.dir\src\gx_ext_overlay.cpp.obj')) {
    "== $o"
    $p = Join-Path $Build $o
    if (Test-Path $p) { dumpbin /symbols $p | Select-String 'ExtOverlaySet|WeakExternal' } else { 'missing' }
  }
  $diag | Tee-Object "$out\link-diagnostics.log" | Select-Object -First 40 | Out-Host
}

python tools/ci/summarize_clang_errors.py $out | Out-Host
Write-Host "##vso[task.uploadsummary]$out\summary.md"
exit 0
