param(
  [ValidateSet("Debug", "Release")]
  [string]$Preset = "Debug",

  [switch]$Clean,

  [switch]$ConfigureOnly,

  [switch]$BuildOnly,

  [switch]$VerboseBuild,

  [string[]]$BuildArgs = @()
)

$ErrorActionPreference = "Stop"

function Write-Step {
  param([string]$Message)
  Write-Host "`n==> $Message" -ForegroundColor Cyan
}

function Assert-CommandExists {
  param([string]$CommandName)

  if (-not (Get-Command $CommandName -ErrorAction SilentlyContinue)) {
    throw "Command '$CommandName' was not found. Please install it and add it to PATH."
  }
}

function Invoke-ExternalCommand {
  param(
    [string]$CommandName,
    [string[]]$Arguments,
    [string]$FailureMessage
  )

  & $CommandName @Arguments
  if ($LASTEXITCODE -ne 0) {
    throw "$FailureMessage (exit code: $LASTEXITCODE)"
  }
}

$ProjectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $ProjectRoot

Assert-CommandExists "cmake"
Assert-CommandExists "ninja"

$BuildDir = Join-Path $ProjectRoot "build/$Preset"

if ($Clean) {
  Write-Step "Cleaning build directory: $BuildDir"
  if (Test-Path $BuildDir) {
    Remove-Item -LiteralPath $BuildDir -Recurse -Force
  }
}

if (-not $BuildOnly) {
  Write-Step "Configuring CMake preset: $Preset"
  Invoke-ExternalCommand "cmake" @("--preset", $Preset) "CMake configure failed"
}

if (-not $ConfigureOnly) {
  Write-Step "Building CMake preset: $Preset"

  $CMakeBuildArgs = @("--build", "--preset", $Preset)
  if ($VerboseBuild) {
    $CMakeBuildArgs += "--verbose"
  }
  if ($BuildArgs.Count -gt 0) {
    $CMakeBuildArgs += "--"
    $CMakeBuildArgs += $BuildArgs
  }

  Invoke-ExternalCommand "cmake" $CMakeBuildArgs "CMake build failed"
}

Write-Host "`nCompile script finished." -ForegroundColor Green
