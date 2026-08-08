param(
    [string]$QtRoot = $env:QTDIR,
    [string]$GStreamerRoot = $env:GSTREAMER_ROOT_X86_64,
    [string]$PortableMsvcRoot = $env:PADMIRROR_MSVC_ROOT,
    [string]$ToolsRoot = "D:\PadMirrorTools",
    [string]$GStreamerVersion = "1.28.5",
    [string]$UxPlayRoot,
    [switch]$EnableIMobileDevice,
    [switch]$SkipInstaller,
    [switch]$InstallMissing = $true
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest
$global:LASTEXITCODE = 0

function Require-Path([string]$Path, [string]$Label) {
    if ([string]::IsNullOrWhiteSpace($Path) -or -not (Test-Path $Path)) {
        throw "$Label was not found: '$Path'"
    }
}

function First-ExistingPath([string[]]$Candidates) {
    foreach ($candidate in $Candidates) {
        if (-not [string]::IsNullOrWhiteSpace($candidate) -and (Test-Path $candidate)) {
            return (Resolve-Path $candidate).Path
        }
    }
    return $null
}

function Import-BatchEnvironment([string]$BatchFile) {
    Require-Path $BatchFile "MSVC environment script"
    $wrapper = Join-Path $env:TEMP ("padmirror-msvc-env-{0}.cmd" -f [Guid]::NewGuid().ToString("N"))
    try {
        [IO.File]::WriteAllLines($wrapper, @(
            "@echo off",
            "call `"$BatchFile`" >nul",
            "if errorlevel 1 exit /b %errorlevel%",
            "set"
        ), [Text.Encoding]::ASCII)
        $global:LASTEXITCODE = 0
        $lines = & cmd.exe /d /c $wrapper
        if (-not $? -or $LASTEXITCODE -ne 0) { throw "Could not initialize the portable MSVC environment" }
        foreach ($line in $lines) {
            $separator = $line.IndexOf('=')
            if ($separator -le 0) { continue }
            $name = $line.Substring(0, $separator)
            $value = $line.Substring($separator + 1)
            [Environment]::SetEnvironmentVariable($name, $value, "Process")
        }
    } finally {
        Remove-Item $wrapper -Force -ErrorAction SilentlyContinue
    }
}

function Find-InnoCompiler {
    return First-ExistingPath @(
        "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe",
        "C:\Program Files (x86)\Inno Setup 6\ISCC.exe"
    )
}

function Download-MicrosoftSigned([string]$Uri, [string]$Destination) {
    if (-not (Test-Path $Destination)) {
        Invoke-WebRequest -Uri $Uri -OutFile $Destination -UseBasicParsing
    }
    $signature = Get-AuthenticodeSignature $Destination
    if ($signature.Status -ne "Valid" -or $signature.SignerCertificate.Subject -notmatch "Microsoft") {
        Remove-Item $Destination -Force
        throw "Microsoft signature verification failed for $Uri"
    }
}

$repoRoot = Split-Path -Parent $PSScriptRoot
$buildDir = Join-Path $repoRoot "build\windows-x64"
$binaryDir = Join-Path $buildDir "src"
$releaseDir = Join-Path $buildDir "stage"
$distDir = Join-Path $repoRoot "dist"
$downloadDir = Join-Path $ToolsRoot "downloads"

if ($InstallMissing) {
    & (Join-Path $PSScriptRoot "install-build-dependencies.ps1") `
        -ToolsRoot $ToolsRoot -GStreamerVersion $GStreamerVersion
}

if ([string]::IsNullOrWhiteSpace($QtRoot)) {
    $QtRoot = First-ExistingPath @(
        (Join-Path $ToolsRoot "Qt\6.8.3\msvc2022_64")
    )
}
if ([string]::IsNullOrWhiteSpace($GStreamerRoot)) {
    $GStreamerRoot = First-ExistingPath @(
        (Join-Path $env:LOCALAPPDATA "Programs\gstreamer\1.0\msvc_x86_64"),
        "C:\gstreamer\1.0\msvc_x86_64"
    )
}
if ([string]::IsNullOrWhiteSpace($PortableMsvcRoot)) {
    $PortableMsvcRoot = First-ExistingPath @((Join-Path $ToolsRoot "msvc"))
}
Require-Path $QtRoot "Qt MSVC root"
Require-Path $GStreamerRoot "GStreamer MSVC root"
Require-Path $PortableMsvcRoot "portable MSVC root"
Require-Path (Join-Path $QtRoot "bin\windeployqt.exe") "windeployqt"

Import-BatchEnvironment (Join-Path $PortableMsvcRoot "setup_x64.bat")
$compilerDir = Split-Path -Parent (Get-Command cl.exe -ErrorAction Stop).Source
$toolScripts = Join-Path $ToolsRoot "aqt\Scripts"
$cmake = Join-Path $toolScripts "cmake.exe"
$ctest = Join-Path $toolScripts "ctest.exe"
$ninja = Join-Path $toolScripts "ninja.exe"
Require-Path $cmake "CMake"
Require-Path $ctest "CTest"
Require-Path $ninja "Ninja"

$gstreamerBin = Join-Path $GStreamerRoot "bin"
$env:PATH = "$compilerDir;$toolScripts;$gstreamerBin;$env:PATH"
$env:PKG_CONFIG_PATH = @(
    (Join-Path $GStreamerRoot "lib\pkgconfig"),
    (Join-Path $GStreamerRoot "share\pkgconfig")
) -join ";"

$imobile = if ($EnableIMobileDevice) { "ON" } else { "OFF" }
$configureArgs = @(
    "-S", $repoRoot,
    "-B", $buildDir,
    "-G", "Ninja",
    "-DCMAKE_BUILD_TYPE=Release",
    "-DCMAKE_MAKE_PROGRAM=$ninja",
    "-DCMAKE_C_COMPILER=cl.exe",
    "-DCMAKE_CXX_COMPILER=cl.exe",
    "-DCMAKE_PREFIX_PATH=$QtRoot",
    "-DPADMIRROR_ENABLE_IMOBILEDEVICE=$imobile",
    "-DPADMIRROR_BUILD_TESTS=ON"
)

& $cmake @configureArgs
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed" }

& $cmake --build $buildDir --parallel
if ($LASTEXITCODE -ne 0) { throw "CMake build failed" }

$msvcRuntimeDlls = @(
    "msvcp140.dll",
    "msvcp140_1.dll",
    "msvcp140_2.dll",
    "vcruntime140.dll",
    "vcruntime140_1.dll"
)
$testBinaryDir = Join-Path $buildDir "tests"
foreach ($runtimeDll in $msvcRuntimeDlls) {
    $runtimePath = Join-Path $compilerDir $runtimeDll
    Require-Path $runtimePath "MSVC runtime $runtimeDll"
    Copy-Item $runtimePath $testBinaryDir -Force
}

& $ctest --test-dir $buildDir --output-on-failure
if ($LASTEXITCODE -ne 0) { throw "Tests failed" }

$builtExe = Join-Path $binaryDir "PadMirror.exe"
$builtRuntimeChecker = Join-Path $binaryDir "PadMirrorRuntimeCheck.exe"
Require-Path $builtExe "PadMirror executable"
Require-Path $builtRuntimeChecker "PadMirror runtime checker"

if (Test-Path $releaseDir) { Remove-Item $releaseDir -Recurse -Force }
New-Item -ItemType Directory -Force -Path $releaseDir | Out-Null
$exe = Join-Path $releaseDir "PadMirror.exe"
$runtimeChecker = Join-Path $releaseDir "PadMirrorRuntimeCheck.exe"
Copy-Item $builtExe $exe -Force
Copy-Item $builtRuntimeChecker $runtimeChecker -Force

& (Join-Path $QtRoot "bin\windeployqt.exe") `
    --release --no-translations --verbose 0 --qmldir (Join-Path $repoRoot "ui") $exe
if ($LASTEXITCODE -ne 0) { throw "windeployqt failed" }

foreach ($runtimeDll in $msvcRuntimeDlls) {
    $runtimePath = Join-Path $compilerDir $runtimeDll
    Require-Path $runtimePath "MSVC runtime $runtimeDll"
    Copy-Item $runtimePath $releaseDir -Force
}

Copy-Item (Join-Path $gstreamerBin "*.dll") $releaseDir -Force
$pluginSource = Join-Path $GStreamerRoot "lib\gstreamer-1.0"
$pluginTarget = Join-Path $releaseDir "gstreamer-1.0"
if (Test-Path $pluginTarget) { Remove-Item $pluginTarget -Recurse -Force }
New-Item -ItemType Directory -Force -Path $pluginTarget | Out-Null
$requiredPlugins = @(
    "gstapp.dll",
    "gstcoreelements.dll",
    "gstvideoparsersbad.dll",
    "gstaudioconvert.dll",
    "gstaudioresample.dll",
    "gstudp.dll",
    "gstrtpmanager.dll",
    "gstrtp.dll",
    "gstd3d11.dll",
    "gstnvcodec.dll",
    "gstlibav.dll",
    "gstwasapi2.dll",
    "gstwasapi.dll"
)
foreach ($plugin in $requiredPlugins) {
    $pluginPath = Join-Path $pluginSource $plugin
    Require-Path $pluginPath "GStreamer plugin $plugin"
    Copy-Item $pluginPath $pluginTarget -Force
}

$scannerSource = Join-Path $GStreamerRoot "libexec\gstreamer-1.0\gst-plugin-scanner.exe"
if (Test-Path $scannerSource) {
    $scannerTarget = Join-Path $releaseDir "libexec\gstreamer-1.0"
    New-Item -ItemType Directory -Force -Path $scannerTarget | Out-Null
    Copy-Item $scannerSource $scannerTarget -Force
}

$dependenciesDir = Join-Path $releaseDir "dependencies"
New-Item -ItemType Directory -Force -Path $dependenciesDir | Out-Null
Copy-Item (Join-Path $PSScriptRoot "repair-runtime.ps1") $dependenciesDir -Force
Copy-Item (Join-Path $PSScriptRoot "configure-wifi-firewall.ps1") $dependenciesDir -Force
Copy-Item (Join-Path $PSScriptRoot "install-usb-capture-driver.ps1") $dependenciesDir -Force
Copy-Item (Join-Path $PSScriptRoot "remove-unsafe-usb-filter.ps1") $dependenciesDir -Force

$usbBridgeDir = Join-Path $releaseDir "usb-bridge"
& (Join-Path $PSScriptRoot "build-usb-bridge.ps1") `
    -ToolsRoot $ToolsRoot -OutputDir $usbBridgeDir
if ($LASTEXITCODE -ne 0) { throw "Safe USB bridge build failed" }

New-Item -ItemType Directory -Force -Path $downloadDir | Out-Null
$vcRedist = Join-Path $downloadDir "VC_redist.x64.exe"
Download-MicrosoftSigned "https://aka.ms/vs/17/release/vc_redist.x64.exe" $vcRedist
Copy-Item $vcRedist $dependenciesDir -Force

if ([string]::IsNullOrWhiteSpace($UxPlayRoot)) {
    $UxPlayRoot = First-ExistingPath @(
        (Join-Path $ToolsRoot "uxplay-bundle"),
        (Join-Path $repoRoot "third_party\uxplay-windows")
    )
}
if ([string]::IsNullOrWhiteSpace($UxPlayRoot)) {
    throw "Patched UxPlay bundle was not found; run scripts\build-uxplay-windows.sh in MSYS2 UCRT64 first"
}
Require-Path (Join-Path $UxPlayRoot "uxplay.exe") `
    "patched UxPlay bundle; run scripts\build-uxplay-windows.sh in MSYS2 UCRT64 first"
$uxplayTarget = Join-Path $releaseDir "uxplay"
if (Test-Path $uxplayTarget) { Remove-Item $uxplayTarget -Recurse -Force }
Copy-Item $UxPlayRoot $uxplayTarget -Recurse -Force

$licenseTarget = Join-Path $releaseDir "licenses\PadMirror"
New-Item -ItemType Directory -Force -Path $licenseTarget | Out-Null
Copy-Item (Join-Path $repoRoot "LICENSES\THIRD_PARTY.md") $licenseTarget -Force
$runtimeReport = Join-Path $env:TEMP "padmirror-runtime-check.txt"
if (Test-Path $runtimeReport) { Remove-Item $runtimeReport -Force }
$runtimeCheck = Start-Process -FilePath $runtimeChecker `
    -ArgumentList @("--report=$runtimeReport") -Wait -PassThru -NoNewWindow
$runtimeCheckExitCode = $runtimeCheck.ExitCode
if ($runtimeCheckExitCode -ne 0 -or -not (Test-Path $runtimeReport)) {
    $details = if (Test-Path $runtimeReport) { Get-Content $runtimeReport -Raw } else { "No report was produced." }
    throw "Packaged runtime check failed:`n$details"
}

New-Item -ItemType Directory -Force -Path $distDir | Out-Null
$portableDir = Join-Path $buildDir "portable-package"
if (Test-Path $portableDir) { Remove-Item $portableDir -Recurse -Force }
Copy-Item $releaseDir $portableDir -Recurse -Force
$portableZip = Join-Path $distDir "PadMirror-portable.zip"
if (Test-Path $portableZip) { Remove-Item $portableZip -Force }
Compress-Archive -Path (Join-Path $portableDir "*") -DestinationPath $portableZip -CompressionLevel Optimal

if (-not $SkipInstaller) {
    $iscc = Find-InnoCompiler
    Require-Path $iscc "Inno Setup compiler"
    & $iscc "/DReleaseDir=$releaseDir" "/DDistDir=$distDir" (Join-Path $repoRoot "installer\PadMirror.iss")
    if ($LASTEXITCODE -ne 0) { throw "Inno Setup packaging failed" }
}

$artifacts = @($portableZip)
if (-not $SkipInstaller) { $artifacts += (Join-Path $distDir "PadMirrorSetup.exe") }
$checksumLines = foreach ($artifact in $artifacts) {
    $hash = (Get-FileHash $artifact -Algorithm SHA256).Hash.ToLowerInvariant()
    "$hash  $(Split-Path -Leaf $artifact)"
}
$checksumLines | Set-Content (Join-Path $distDir "SHA256SUMS.txt") -Encoding ASCII

Write-Host "PadMirror executable: $exe"
Write-Host "Portable package: $portableZip"
if (-not $SkipInstaller) { Write-Host "Installer: $(Join-Path $distDir 'PadMirrorSetup.exe')" }
