param(
    [string]$ToolsRoot = "D:\PadMirrorTools",
    [string]$QtVersion = "6.8.3",
    [string]$GStreamerVersion = "1.28.5",
    [string]$LibusbVersion = "1.0.30"
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

function Download-File([string]$Uri, [string]$Destination) {
    if (-not (Test-Path $Destination)) {
        Invoke-WebRequest -Uri $Uri -OutFile $Destination -UseBasicParsing
    }
}

function Assert-Sha256([string]$Path, [string]$Expected) {
    $actual = (Get-FileHash -Path $Path -Algorithm SHA256).Hash.ToUpperInvariant()
    if ($actual -ne $Expected.ToUpperInvariant()) {
        Remove-Item $Path -Force
        throw "SHA-256 verification failed: $Path"
    }
}

function Download-WithPublishedHash([string]$Uri, [string]$Destination) {
    $checksumText = (Invoke-WebRequest -Uri "$Uri.sha256sum" -UseBasicParsing).Content.Trim()
    $expected = ($checksumText -split "\s+")[0]
    if ($expected -notmatch "^[0-9A-Fa-f]{64}$") {
        throw "Invalid checksum published for $Uri"
    }
    Download-File $Uri $Destination
    Assert-Sha256 $Destination $expected
}

function Find-InnoCompiler {
    $candidates = @(
        "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe",
        "C:\Program Files (x86)\Inno Setup 6\ISCC.exe"
    )
    return $candidates | Where-Object { Test-Path $_ } | Select-Object -First 1
}

New-Item -ItemType Directory -Force -Path $ToolsRoot | Out-Null
$downloads = Join-Path $ToolsRoot "downloads"
New-Item -ItemType Directory -Force -Path $downloads | Out-Null

$python = (Get-Command python.exe -ErrorAction Stop).Source
$portableMsvcRoot = Join-Path $ToolsRoot "msvc"
if (-not (Test-Path (Join-Path $portableMsvcRoot "setup_x64.bat"))) {
    $portableMsvcScript = Join-Path $ToolsRoot "portable-msvc.py"
    $portableMsvcUri = "https://gist.githubusercontent.com/mmozeiko/7f3162ec2988e81e56d5c4e22cde9977/raw/69a4e56e9fe5dd79f96ba5028016aa85742528ef/portable-msvc.py"
    Download-File $portableMsvcUri $portableMsvcScript
    Assert-Sha256 $portableMsvcScript "84bc7fa28a45081f50b1144d14471264bef22b248816387de3d614b09ce10b59"
    Push-Location $ToolsRoot
    try {
        & $python $portableMsvcScript --vs 2022 --target x64 --host x64 --accept-license
        if ($LASTEXITCODE -ne 0) { throw "Portable MSVC installation failed" }
    } finally {
        Pop-Location
    }
}

$aqtRoot = Join-Path $ToolsRoot "aqt"
$aqtPython = Join-Path $aqtRoot "Scripts\python.exe"
if (-not (Test-Path $aqtPython)) {
    & $python -m venv $aqtRoot
    if ($LASTEXITCODE -ne 0) { throw "Python virtual environment creation failed" }
}
& $aqtPython -m pip install --upgrade pip aqtinstall cmake ninja
if ($LASTEXITCODE -ne 0) { throw "Python build tools installation failed" }

$qtInstallRoot = Join-Path $ToolsRoot "Qt"
$qtRoot = Join-Path $qtInstallRoot "$QtVersion\msvc2022_64"
if (-not (Test-Path (Join-Path $qtRoot "bin\windeployqt.exe"))) {
    & $aqtPython -m aqt install-qt windows desktop $QtVersion win64_msvc2022_64 -O $qtInstallRoot
    if ($LASTEXITCODE -ne 0) { throw "Qt installation failed" }
}

$gstreamerRoot = Join-Path $env:LOCALAPPDATA "Programs\gstreamer\1.0\msvc_x86_64"
if (-not (Test-Path (Join-Path $gstreamerRoot "lib\pkgconfig\gstreamer-1.0.pc"))) {
    $gstreamerName = "gstreamer-1.0-msvc-x86_64-$GStreamerVersion.exe"
    $gstreamerInstaller = Join-Path $downloads $gstreamerName
    $gstreamerUri = "https://gstreamer.freedesktop.org/data/pkg/windows/$GStreamerVersion/msvc/$gstreamerName"
    Download-WithPublishedHash $gstreamerUri $gstreamerInstaller
    $process = Start-Process -FilePath $gstreamerInstaller -ArgumentList @(
        "/CURRENTUSER", "/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART", "/SP-"
    ) -Wait -PassThru
    if ($process.ExitCode -ne 0) { throw "GStreamer installation failed with exit $($process.ExitCode)" }
}

$libusbRoot = Join-Path $ToolsRoot "libusb-$LibusbVersion"
if (-not (Test-Path (Join-Path $libusbRoot "VS2022\MS64\dll\libusb-1.0.dll"))) {
    if ($LibusbVersion -ne "1.0.30") {
        throw "Update the pinned libusb checksum before using version $LibusbVersion"
    }
    $libusbArchive = Join-Path $downloads "libusb-$LibusbVersion.7z"
    Download-File "https://github.com/libusb/libusb/releases/download/v$LibusbVersion/libusb-$LibusbVersion.7z" $libusbArchive
    Assert-Sha256 $libusbArchive "7fb1dfec805b97983763d7d0ae244320da12add1003d4249c96cc4d586398c79"

    $sevenZip = Join-Path $ToolsRoot "7zr.exe"
    Download-File "https://www.7-zip.org/a/7zr.exe" $sevenZip
    Assert-Sha256 $sevenZip "ef323796edb615d8928378d21e88b26ace9915c0a2e7206e584e4302a93cfbcf"
    & $sevenZip x -y "-o$libusbRoot" $libusbArchive
    if ($LASTEXITCODE -ne 0) { throw "libusb extraction failed" }
}

if (-not (Find-InnoCompiler)) {
    $winget = (Get-Command winget.exe -ErrorAction Stop).Source
    & $winget install --id JRSoftware.InnoSetup --exact --source winget --scope user --silent `
        --disable-interactivity --accept-package-agreements --accept-source-agreements
    if ($LASTEXITCODE -ne 0) { throw "Inno Setup installation failed" }
}

$env:QTDIR = $qtRoot
$env:GSTREAMER_ROOT_X86_64 = $gstreamerRoot
$env:PADMIRROR_MSVC_ROOT = $portableMsvcRoot
$env:PADMIRROR_LIBUSB_ROOT = $libusbRoot
[Environment]::SetEnvironmentVariable("QTDIR", $qtRoot, "User")
[Environment]::SetEnvironmentVariable("GSTREAMER_ROOT_X86_64", $gstreamerRoot, "User")
[Environment]::SetEnvironmentVariable("PADMIRROR_MSVC_ROOT", $portableMsvcRoot, "User")
[Environment]::SetEnvironmentVariable("PADMIRROR_LIBUSB_ROOT", $libusbRoot, "User")

Write-Host "Build dependencies are ready."
Write-Host "QTDIR=$qtRoot"
Write-Host "GSTREAMER_ROOT_X86_64=$gstreamerRoot"
Write-Host "PADMIRROR_MSVC_ROOT=$portableMsvcRoot"
Write-Host "PADMIRROR_LIBUSB_ROOT=$libusbRoot"
