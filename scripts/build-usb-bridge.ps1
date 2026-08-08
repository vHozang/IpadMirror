param(
    [string]$ToolsRoot = "D:\PadMirrorTools",
    [Parameter(Mandatory = $true)][string]$OutputDir
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest
$global:LASTEXITCODE = 0

function Require-Path([string]$Path, [string]$Label) {
    if ([string]::IsNullOrWhiteSpace($Path) -or -not (Test-Path $Path)) {
        throw "$Label was not found: '$Path'"
    }
}

$repoRoot = Split-Path -Parent $PSScriptRoot
$source = Join-Path $repoRoot "usb_bridge\PadMirrorUsbBridge.py"
Require-Path $source "USB bridge source"

$bootstrapPython = (Get-Command python.exe -ErrorAction Stop).Source
$bootstrapVenv = Join-Path $ToolsRoot "usb-bridge-bootstrap"
$uv = Join-Path $bootstrapVenv "Scripts\uv.exe"
$bootstrapVenvPython = Join-Path $bootstrapVenv "Scripts\python.exe"
if (-not (Test-Path $uv)) {
    & $bootstrapPython -m venv $bootstrapVenv
    if ($LASTEXITCODE -ne 0) { throw "Could not create the USB bridge bootstrap environment" }
    & $bootstrapVenvPython -m pip install --upgrade pip "uv>=0.8,<1"
    if ($LASTEXITCODE -ne 0) { throw "Could not install the verified Python environment manager" }
}

$bridgeVenv = Join-Path $ToolsRoot "usb-bridge-python312"
$bridgePython = Join-Path $bridgeVenv "Scripts\python.exe"
if (-not (Test-Path $bridgePython)) {
    & $uv python install 3.12
    if ($LASTEXITCODE -ne 0) { throw "Could not download app-local Python 3.12" }
    & $uv venv --python 3.12 $bridgeVenv
    if ($LASTEXITCODE -ne 0) { throw "Could not provision app-local Python 3.12" }
}

& $uv pip install --python $bridgePython --upgrade `
    "pymobiledevice3==10.5.0" "pyinstaller>=6.16,<7"
if ($LASTEXITCODE -ne 0) { throw "Could not install the safe USB bridge dependencies" }

$buildRoot = Join-Path $repoRoot "build\usb-bridge"
$distRoot = Join-Path $buildRoot "dist"
$workRoot = Join-Path $buildRoot "work"
$specRoot = Join-Path $buildRoot "spec"
New-Item -ItemType Directory -Force -Path $distRoot, $workRoot, $specRoot | Out-Null

& $bridgePython -m PyInstaller `
    --noconfirm `
    --onedir `
    --console `
    --name PadMirrorUsbBridge `
    --distpath $distRoot `
    --workpath $workRoot `
    --specpath $specRoot `
    --collect-data pymobiledevice3 `
    --collect-data developer_disk_image `
    --collect-all pytun_pmd3 `
    --recursive-copy-metadata pymobiledevice3 `
    --copy-metadata developer_disk_image `
    --copy-metadata pyimg4 `
    --hidden-import usb.backend.libusb1 `
    $source
if ($LASTEXITCODE -ne 0) { throw "USB bridge packaging failed" }

$builtBridge = Join-Path $distRoot "PadMirrorUsbBridge"
$builtExe = Join-Path $builtBridge "PadMirrorUsbBridge.exe"
Require-Path $builtExe "packaged USB bridge"
if (Test-Path $OutputDir) { Remove-Item $OutputDir -Recurse -Force }
Copy-Item $builtBridge $OutputDir -Recurse -Force

$probeExe = Join-Path $OutputDir "PadMirrorUsbBridge.exe"
& $probeExe --probe
if ($LASTEXITCODE -ne 0) { throw "Packaged USB bridge dependency check failed" }

$sourceDir = Join-Path $OutputDir "source"
New-Item -ItemType Directory -Force -Path $sourceDir | Out-Null
& $bootstrapVenvPython -m pip download --no-deps --no-binary=:all: `
    --dest $sourceDir "pymobiledevice3==10.5.0"
if ($LASTEXITCODE -ne 0) { throw "Could not package pymobiledevice3 corresponding source" }
Copy-Item $source (Join-Path $sourceDir "PadMirrorUsbBridge.py") -Force

Write-Host "Safe Apple USB bridge: $probeExe"
