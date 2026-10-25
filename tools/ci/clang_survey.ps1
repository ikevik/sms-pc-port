# Finds out how far the game builds with LLVM clang targeting x86_64-pc-windows-msvc.
# Windows only. Configures with the repo's own CMake (graphics layer off), builds with
# `ninja -k 0` so every translation unit is tried, and summarises the errors.
# Always exits 0 so the logs and summary can be published; read summary.md for the result.
param(
  [string]$Build = 'build-clang',
  [int]$TimeoutMin = 50
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
cmd /c "`"$vs\VC\Auxiliary\Build\vcvars64.bat`" >nul 2>&1 && set" | ForEach-Object {
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

Say 'configure'
cmake -S . -B $Build -G Ninja `
  -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ `
  -DCMAKE_BUILD_TYPE=RelWithDebInfo -DSMS_ARCH=64 -DSMS_WITH_GX=OFF `
  -DSMS_GAME_CXX_STD=gnu++14 -DSMS_PLATFORM_CXX_STD=gnu++14 `
  '-DCMAKE_CXX_FLAGS=-D_USE_MATH_DEFINES -D_CRT_SECURE_NO_WARNINGS -D_CRT_NONSTDC_NO_DEPRECATE' *>&1 |
  Tee-Object "$out\configure.log" | Select-Object -Last 40 | Out-Host
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

python tools/ci/summarize_clang_errors.py $out | Out-Host
Write-Host "##vso[task.uploadsummary]$out\summary.md"
exit 0
