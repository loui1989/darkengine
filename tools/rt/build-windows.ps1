<#
.SYNOPSIS
  Builds ThiefRunner with the optional path tracer (RTGL1) for Windows x64.

.DESCRIPTION
  1. Gets RTGL1 (https://github.com/loui1989/RayTracedGL1, branch thief) next to
     this repository, unless -Rtgl1 names a checkout, and builds RTGL1.dll with
     its Vulkan backend and its shaders (SPIR-V).
  2. Configures and builds the game with DARK_RTGL1_INCLUDE (cmake/RayTracing.cmake).
  3. Puts the rt folder next to thief-runner.exe (the library, the shaders, the
     path tracer's data files) and writes thief-rt.cmd, which starts the game
     with the path tracer on and its log in thief-rt.log.

  Run it from a "Developer PowerShell for VS" with C:\msys64\usr\bin on PATH
  (the README's Windows prerequisites), plus Python 3 for RTGL1's shader script.
  See docs/path-tracer.md.

.PARAMETER Rtgl1
  An RTGL1 checkout to use. Default: ..\RayTracedGL1 beside this repository,
  cloned when missing.

.PARAMETER BuildDir
  The game's build folder, under this repository. Default: build-win64-rt.

.PARAMETER Dlss
  Also build NVIDIA DLSS (and DLSS Ray Reconstruction) into RTGL1, from the
  DLSS SDK (https://github.com/NVIDIA/DLSS), cloned beside this repository
  when -DlssSdk does not name one. Without it the upscaler is AMD FSR2.

.PARAMETER Generator
  A CMake generator, as "Visual Studio 17 2022". Default: CMake's newest
  Visual Studio.

.PARAMETER SkipGame
  Only build RTGL1 and refresh the rt folder.
#>
param(
  [string]$Rtgl1 = "",
  [string]$BuildDir = "build-win64-rt",
  [switch]$Dlss,
  [string]$DlssSdk = "",
  [string]$Generator = "",
  [switch]$SkipGame
)

$ErrorActionPreference = "Stop"
$Repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$Parent = Split-Path $Repo -Parent
if (-not $Rtgl1) { $Rtgl1 = Join-Path $Parent "RayTracedGL1" }

function Step([string]$Text) { Write-Host "`n== $Text" -ForegroundColor Cyan }
function Invoke-Checked([string]$Exe, [string[]]$Arguments) {
  & $Exe @Arguments
  if ($LASTEXITCODE -ne 0) { throw "$Exe $($Arguments -join ' ') failed (exit code $LASTEXITCODE)" }
}

$Gen = @()
if ($Generator) { $Gen = @("-G", $Generator) }

foreach ($Tool in @("git", "cmake")) {
  if (-not (Get-Command $Tool -ErrorAction SilentlyContinue)) { throw "$Tool is not on PATH" }
}
$Python = $null
foreach ($Candidate in @("py", "python", "python3")) {
  if (Get-Command $Candidate -ErrorAction SilentlyContinue) { $Python = $Candidate; break }
}
if (-not $Python) { throw "Python 3 is not on PATH (RTGL1's shaders are built by a Python script)" }

# --- RTGL1 ------------------------------------------------------------------
Step "RTGL1 ($Rtgl1)"
if (-not (Test-Path (Join-Path $Rtgl1 "CMakeLists.txt"))) {
  Invoke-Checked git @("clone", "--recursive", "--branch", "thief", "https://github.com/loui1989/RayTracedGL1", $Rtgl1)
} else {
  # (the thief branch's fixes: brought in when the checkout is on it)
  $Branch = (& git -C $Rtgl1 rev-parse --abbrev-ref HEAD).Trim()
  if ($Branch -eq "thief") { Invoke-Checked git @("-C", $Rtgl1, "pull", "--ff-only") }
  Invoke-Checked git @("-C", $Rtgl1, "submodule", "update", "--init", "--recursive")
}

$DlssArgs = @("-DRG_WITH_NATIVE_DLSS=OFF")
if ($Dlss) {
  if (-not $DlssSdk) { $DlssSdk = Join-Path $Parent "DLSS" }
  if (-not (Test-Path (Join-Path $DlssSdk "include\nvsdk_ngx.h"))) {
    Step "NVIDIA DLSS SDK ($DlssSdk)"
    Invoke-Checked git @("clone", "--depth", "1", "https://github.com/NVIDIA/DLSS", $DlssSdk)
    Invoke-Checked git @("-C", $DlssSdk, "lfs", "pull")
  }
  $DlssArgs = @("-DRG_WITH_NATIVE_DLSS=ON", "-DDLSS_SDK_PATH=$DlssSdk")
}

$RtBuild = Join-Path $Rtgl1 "build-win64"
Invoke-Checked cmake (@("-S", $Rtgl1, "-B", $RtBuild) + $Gen + @(
  "-A", "x64",
  "-DCMAKE_POLICY_VERSION_MINIMUM=3.5",
  "-DRG_WITH_SURFACE_WIN32=ON",
  "-DRG_WITH_EXAMPLES=OFF",
  "-DRG_WITH_IMGUI=OFF",
  "-DRG_WITH_DX12=OFF",
  "-DRG_WITH_NRD=OFF") + $DlssArgs)
Invoke-Checked cmake @("--build", $RtBuild, "--config", "Release", "--target", "RayTracedGL1", "--parallel")
$Dll = Join-Path $Rtgl1 "Build\bin\RTGL1.dll"
if (-not (Test-Path $Dll)) { throw "RTGL1.dll was not built ($Dll)" }

Step "RTGL1 shaders"
Push-Location (Join-Path $Rtgl1 "Source\Shaders")
try {
  $PyArgs = @("GenerateShaders.py", "-r")
  if ($Python -eq "py") { $PyArgs = @("-3") + $PyArgs }
  $ShaderLog = & $Python @PyArgs 2>&1 | Out-String
  Write-Host $ShaderLog
  if ($LASTEXITCODE -ne 0 -or $ShaderLog -match "failed|Traceback") { throw "RTGL1's shaders did not build" }
} finally {
  Pop-Location
}

# --- the game ---------------------------------------------------------------
$Build = Join-Path $Repo $BuildDir
if (-not $SkipGame) {
  Step "ThiefRunner ($Build)"
  Invoke-Checked git @("-C", $Repo, "submodule", "update", "--init", "--recursive")
  Invoke-Checked cmake (@("-S", $Repo, "-B", $Build) + $Gen + @(
    "-A", "x64",
    "-DDARK_RTGL1_INCLUDE=$(Join-Path $Rtgl1 'Include')"))
  Invoke-Checked cmake @("--build", $Build, "--config", "Release", "--target", "thief2", "--parallel")
}
$Exe = Get-ChildItem -Path $Build -Recurse -Filter "thief-runner.exe" -ErrorAction SilentlyContinue |
  Sort-Object LastWriteTime -Descending | Select-Object -First 1
if (-not $Exe) { throw "thief-runner.exe was not found under $Build" }
$Out = $Exe.DirectoryName

# --- the rt folder ----------------------------------------------------------
Step "rt folder ($Out\rt)"
$Rt = Join-Path $Out "rt"
New-Item -ItemType Directory -Force -Path (Join-Path $Rt "bin"), (Join-Path $Rt "shaders"), (Join-Path $Rt "glass") | Out-Null
Copy-Item $Dll (Join-Path $Rt "bin") -Force
# AMD FidelityFX (FSR2) runtime, which RTGL1 loads from its own folder
Get-ChildItem (Join-Path $Rtgl1 "Source\bin") -Filter "ffx_*.dll" | Copy-Item -Destination (Join-Path $Rt "bin") -Force
if ($Dlss) {
  foreach ($Name in @("nvngx_dlss.dll", "nvngx_dlssd.dll")) {
    $Found = Get-ChildItem (Join-Path $DlssSdk "lib\Windows_x86_64\rel") -Filter $Name -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($Found) { Copy-Item $Found.FullName (Join-Path $Rt "bin") -Force } else { Write-Warning "$Name not found in the DLSS SDK" }
  }
}
Copy-Item (Join-Path $Rtgl1 "Build\shaders\*.spv") (Join-Path $Rt "shaders") -Force
Copy-Item (Join-Path $Rtgl1 "Tools\*.ktx2") $Rt -Force
Copy-Item (Join-Path $Repo "assets\rt\glass\*.png") (Join-Path $Rt "glass") -Force
# developerMode: RTGL1 loads the material maps the game makes (rt\mat_dev, PNG)
# only with it on -- off, only pre-compressed KTX2: no bumps, roughness or
# crevices, and the flames' glow maps missing (each flame sprite glowing as a
# whole square). debugWindows keeps its debug window off.
$Json = Join-Path $Rt "RTGL1.json"
if (Test-Path $Json) {
  $Text = Get-Content -Raw $Json
  if ($Text -match '"developerMode"\s*:\s*false') {
    $Text = $Text -replace '"developerMode"\s*:\s*false', '"developerMode": true'
    if ($Text -notmatch '"debugWindows"') {
      $Text = $Text -replace '"developerMode": true', ('"developerMode": true,' + "`n" + '  "debugWindows": false')
    }
    Set-Content -Path $Json -Encoding ascii -Value $Text
    Write-Host "RTGL1.json: developerMode turned on (the game's material maps)"
  }
} else {
  Set-Content -Path $Json -Encoding ascii -Value @'
{
  "version": 0,
  "developerMode": true,
  "debugWindows": false,
  "vulkanValidation": false,
  "dx12Validation": false,
  "dlssValidation": false,
  "fpsMonitor": false
}
'@
}

$Cmd = Join-Path $Out "thief-rt.cmd"
Set-Content -Path $Cmd -Encoding ascii -Value @'
@echo off
rem ThiefRunner with the path tracer (rt folder beside this file); the log is thief-rt.log
set THIEF_RT=1
"%~dp0thief-runner.exe" %* > "%~dp0thief-rt.log" 2>&1
'@

Step "Done"
Write-Host "Game:  $($Exe.FullName)"
Write-Host "Start: $Cmd  (from the game's folder, or choose it when asked)"
