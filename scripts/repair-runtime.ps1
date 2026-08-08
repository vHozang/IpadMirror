param(
    [switch]$ForceGStreamer,
    [string]$GStreamerVersion = "1.28.5"
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

function Test-VcRuntime {
    $key = "HKLM:\SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64"
    if (-not (Test-Path $key)) { return $false }
    return (Get-ItemPropertyValue -Path $key -Name Installed -ErrorAction SilentlyContinue) -eq 1
}

function Start-ElevatedInstaller([string]$Path, [string[]]$Arguments) {
    if (-not (Test-Path $Path)) {
        throw "Required installer was not found: $Path"
    }
    $process = Start-Process -FilePath $Path -ArgumentList $Arguments -Verb RunAs -Wait -PassThru
    if ($process.ExitCode -notin @(0, 3010)) {
        throw "Installer failed with exit code $($process.ExitCode): $Path"
    }
}

$dependencyRoot = $PSScriptRoot
$applicationRoot = Split-Path -Parent $dependencyRoot

$vcRedist = Join-Path $dependencyRoot "VC_redist.x64.exe"
if (-not (Test-VcRuntime)) {
    Start-ElevatedInstaller $vcRedist @("/install", "/quiet", "/norestart")
}

$gstreamerRoots = @(
    $env:GSTREAMER_1_0_ROOT_MSVC_X86_64,
    (Join-Path $env:LOCALAPPDATA "Programs\gstreamer\1.0\msvc_x86_64"),
    "C:\gstreamer\1.0\msvc_x86_64"
) | Where-Object { -not [string]::IsNullOrWhiteSpace($_) }
$gstreamerReady = $false
foreach ($root in $gstreamerRoots) {
    if (Test-Path (Join-Path $root "lib\gstreamer-1.0\gstd3d11.dll")) {
        $gstreamerReady = $true
        break
    }
}

if ($ForceGStreamer -or -not $gstreamerReady) {
    $gstreamerInstaller = Get-ChildItem $dependencyRoot -Filter "gstreamer-1.0-msvc-x86_64-*.exe" |
        Sort-Object Name -Descending |
        Select-Object -First 1
    if ($null -eq $gstreamerInstaller) {
        $downloadRoot = Join-Path $env:TEMP "PadMirror"
        New-Item -ItemType Directory -Force -Path $downloadRoot | Out-Null
        $installerName = "gstreamer-1.0-msvc-x86_64-$GStreamerVersion.exe"
        $installerPath = Join-Path $downloadRoot $installerName
        $baseUri = "https://gstreamer.freedesktop.org/data/pkg/windows/$GStreamerVersion/msvc"
        $installerUri = "$baseUri/$installerName"
        $checksum = ((Invoke-WebRequest -Uri "$installerUri.sha256sum" -UseBasicParsing).Content.Trim() -split "\s+")[0]
        if ($checksum -notmatch "^[0-9A-Fa-f]{64}$") { throw "Invalid GStreamer checksum" }
        if (-not (Test-Path $installerPath)) {
            Invoke-WebRequest -Uri $installerUri -OutFile $installerPath -UseBasicParsing
        }
        if ((Get-FileHash $installerPath -Algorithm SHA256).Hash -ne $checksum) {
            Remove-Item $installerPath -Force
            throw "GStreamer installer checksum verification failed"
        }
        $gstreamerInstaller = Get-Item $installerPath
    }
    $process = Start-Process -FilePath $gstreamerInstaller.FullName -ArgumentList @(
        "/CURRENTUSER", "/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART", "/SP-"
    ) -Wait -PassThru
    if ($process.ExitCode -ne 0) { throw "GStreamer installer failed with exit $($process.ExitCode)" }

    $installedRoot = Join-Path $env:LOCALAPPDATA "Programs\gstreamer\1.0\msvc_x86_64"
    Copy-Item (Join-Path $installedRoot "bin\*.dll") $applicationRoot -Force
    $pluginTarget = Join-Path $applicationRoot "gstreamer-1.0"
    New-Item -ItemType Directory -Force -Path $pluginTarget | Out-Null
    foreach ($plugin in @(
        "gstapp.dll", "gstcoreelements.dll", "gstvideoparsersbad.dll",
        "gstaudioconvert.dll", "gstaudioresample.dll", "gstudp.dll",
        "gstrtpmanager.dll", "gstrtp.dll", "gstd3d11.dll", "gstnvcodec.dll",
        "gstlibav.dll",
        "gstwasapi2.dll", "gstwasapi.dll"
    )) {
        Copy-Item (Join-Path $installedRoot "lib\gstreamer-1.0\$plugin") $pluginTarget -Force
    }
    $scannerTarget = Join-Path $applicationRoot "libexec\gstreamer-1.0"
    New-Item -ItemType Directory -Force -Path $scannerTarget | Out-Null
    Copy-Item (Join-Path $installedRoot "libexec\gstreamer-1.0\gst-plugin-scanner.exe") $scannerTarget -Force
}

$scannerTarget = Join-Path $applicationRoot "libexec\gstreamer-1.0"
if (Test-Path (Join-Path $scannerTarget "gst-plugin-scanner.exe")) {
    foreach ($runtimeDll in @(
        "msvcp140.dll", "msvcp140_1.dll", "msvcp140_2.dll",
        "msvcp140_atomic_wait.dll", "msvcp140_codecvt_ids.dll",
        "vcruntime140.dll", "vcruntime140_1.dll", "vcruntime140_threads.dll"
    )) {
        $runtimePath = Join-Path $applicationRoot $runtimeDll
        if (Test-Path $runtimePath) {
            Copy-Item $runtimePath $scannerTarget -Force
        }
    }
}

Write-Host "PadMirror runtime repair completed."
