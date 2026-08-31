param(
    [string]$QtRoot = $env:QTDIR,
    [string]$GStreamerRoot = $env:GSTREAMER_ROOT_X86_64,
    [string]$PortableMsvcRoot = $env:PADMIRROR_MSVC_ROOT,
    [string]$ToolsRoot = "D:\PadMirrorTools",
    [string]$GStreamerVersion = "1.28.5",
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
        $lines = & $env:ComSpec /d /s /c "`"$wrapper`""
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
    try {
        Import-Module Microsoft.PowerShell.Security -ErrorAction SilentlyContinue
        $signature = Get-AuthenticodeSignature $Destination -ErrorAction SilentlyContinue
        if ($signature -and ($signature.Status -ne "Valid" -or $signature.SignerCertificate.Subject -notmatch "Microsoft")) {
            Remove-Item $Destination -Force
            throw "Microsoft signature verification failed for $Uri"
        }
    } catch {
        # Continue if authenticode security module cannot be loaded in non-interactive batch
    }
}

function Get-FileHashWithRetry([string]$Path) {
    for ($attempt = 0; $attempt -lt 40; $attempt++) {
        try {
            $stream = [System.IO.File]::OpenRead($Path)
            try {
                $sha = [System.Security.Cryptography.SHA256]::Create()
                try {
                    $hash = $sha.ComputeHash($stream)
                    $hex = ([System.BitConverter]::ToString($hash)).Replace("-", "").ToLowerInvariant()
                    return [PSCustomObject]@{ Hash = $hex }
                } finally {
                    $sha.Dispose()
                }
            } finally {
                $stream.Dispose()
            }
        } catch [System.IO.IOException], [System.UnauthorizedAccessException] {
            Start-Sleep -Milliseconds 250
        }
    }
    throw "Could not read release artifact for hashing: $Path"
}

function Invoke-NativeProcess(
    [string]$FilePath,
    [string[]]$Arguments,
    [string]$FailureMessage
) {
    $quotedArguments = foreach ($argument in $Arguments) {
        if ($argument -match '[\s"]') {
            '"' + $argument.Replace('"', '\"') + '"'
        } else {
            $argument
        }
    }
    $startInfo = New-Object System.Diagnostics.ProcessStartInfo
    $startInfo.FileName = $FilePath
    $startInfo.Arguments = $quotedArguments -join ' '
    $startInfo.UseShellExecute = $false
    $process = New-Object System.Diagnostics.Process
    $process.StartInfo = $startInfo
    if (-not $process.Start()) { throw "Could not start $FilePath" }
    $process.WaitForExit()
    if ($process.ExitCode -ne 0) { throw $FailureMessage }
}

function Assert-NoForbiddenPayload([string]$Root) {
    # PadMirror V1 must never ship driver binaries, libusb/UsbDk, the retired
    # Python USB bridge, an external uxplay.exe, standalone UxPlay bundles, or
    # OBS artifacts. Fail the release if any of them are staged.
    $forbidden = @(
        Get-ChildItem -LiteralPath $Root -Recurse -Force -File | Where-Object {
            $_.Name -match '^(?i:wintun\.dll|libusb.*\.dll|uxplay\.exe|.*\.sys|.*\.inf|PadMirrorUsbBridge.*|PadMirrorWinUsbSetup\.exe)$' -or
            $_.Name -match '(?i)obs-gstreamer' -or
            $_.FullName -match '(?i)[\\/](UsbDk|usb-bridge|uxplay|obs-plugin)(?:[\\/]|$)'
        }
    )
    if ($forbidden.Count -ne 0) {
        $paths = ($forbidden | ForEach-Object FullName) -join "`n"
        throw "Forbidden payload was packaged:`n$paths"
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
if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
    $compilerPath = Get-ChildItem (Join-Path $PortableMsvcRoot "VC\Tools\MSVC\*\bin\Hostx64\x64\cl.exe") |
        Sort-Object FullName -Descending | Select-Object -First 1
    $sdkVersionDir = Get-ChildItem (Join-Path $PortableMsvcRoot "Windows Kits\10\bin") -Directory |
        Where-Object { Test-Path (Join-Path $_.FullName "x64\rc.exe") } |
        Sort-Object Name -Descending | Select-Object -First 1
    if (-not $compilerPath -or -not $sdkVersionDir) {
        throw "Portable MSVC files are incomplete under $PortableMsvcRoot"
    }
    $vcToolsRoot = $compilerPath.Directory.Parent.Parent.Parent.FullName
    $sdkRoot = Join-Path $PortableMsvcRoot "Windows Kits\10"
    $sdkVersion = $sdkVersionDir.Name
    $env:PATH = @(
        $compilerPath.Directory.FullName,
        (Join-Path $sdkVersionDir.FullName "x64"),
        $env:PATH
    ) -join ";"
    $env:INCLUDE = @(
        (Join-Path $vcToolsRoot "include"),
        (Join-Path $sdkRoot "Include\$sdkVersion\ucrt"),
        (Join-Path $sdkRoot "Include\$sdkVersion\shared"),
        (Join-Path $sdkRoot "Include\$sdkVersion\um"),
        (Join-Path $sdkRoot "Include\$sdkVersion\winrt"),
        (Join-Path $sdkRoot "Include\$sdkVersion\cppwinrt")
    ) -join ";"
    $env:LIB = @(
        (Join-Path $vcToolsRoot "lib\x64"),
        (Join-Path $sdkRoot "Lib\$sdkVersion\ucrt\x64"),
        (Join-Path $sdkRoot "Lib\$sdkVersion\um\x64")
    ) -join ";"
}
$compilerDir = Split-Path -Parent (Get-Command cl.exe -ErrorAction Stop).Source
$toolScripts = Join-Path $ToolsRoot "aqt\Scripts"
$cmake = First-ExistingPath @(
    (Join-Path $ToolsRoot "aqt\Lib\site-packages\cmake\data\bin\cmake.exe"),
    (Join-Path $toolScripts "cmake.exe")
)
$ctest = First-ExistingPath @(
    (Join-Path $ToolsRoot "aqt\Lib\site-packages\cmake\data\bin\ctest.exe"),
    (Join-Path $toolScripts "ctest.exe")
)
$ninja = Join-Path $toolScripts "ninja.exe"
Require-Path $cmake "CMake"
Require-Path $ctest "CTest"
Require-Path $ninja "Ninja"

$gstreamerBin = Join-Path $GStreamerRoot "bin"
$qtBin = Join-Path $QtRoot "bin"
$env:PATH = "$compilerDir;$toolScripts;$qtBin;$gstreamerBin;$env:PATH"
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
    "-DPADMIRROR_OPENSSL_ROOT=$GStreamerRoot",
    "-DPADMIRROR_BUILD_TESTS=ON"
)

Invoke-NativeProcess $cmake $configureArgs "CMake configure failed"

Invoke-NativeProcess $ninja @("-C", $buildDir) "Ninja build failed"

$msvcRuntimeDlls = @(
    "msvcp140.dll",
    "msvcp140_1.dll",
    "msvcp140_2.dll",
    "msvcp140_atomic_wait.dll",
    "msvcp140_codecvt_ids.dll",
    "vcruntime140.dll",
    "vcruntime140_1.dll",
    "vcruntime140_threads.dll"
)
$testBinaryDir = Join-Path $buildDir "tests"
foreach ($runtimeDll in $msvcRuntimeDlls) {
    $runtimePath = Join-Path $compilerDir $runtimeDll
    Require-Path $runtimePath "MSVC runtime $runtimeDll"
    $targetPath = Join-Path $testBinaryDir $runtimeDll
    if (-not (Test-Path $targetPath)) {
        Copy-Item $runtimePath $testBinaryDir -Force -ErrorAction SilentlyContinue
    }
}

Invoke-NativeProcess $ctest @("--test-dir", $buildDir, "--output-on-failure") "Tests failed"

$builtExe = Join-Path $binaryDir "PadMirror.exe"
$builtRuntimeChecker = Join-Path $binaryDir "PadMirrorRuntimeCheck.exe"
Require-Path $builtExe "PadMirror executable"
Require-Path $builtRuntimeChecker "PadMirror runtime checker"

if (Test-Path $releaseDir) {
    try {
        Remove-Item $releaseDir -Recurse -Force -ErrorAction Stop
    } catch {
        # If files in stage are in use by background processes, clean non-locked files
        Get-ChildItem $releaseDir -Recurse -ErrorAction SilentlyContinue |
            Where-Object { -not $_.PSIsContainer } |
            ForEach-Object {
                try { Remove-Item $_.FullName -Force -ErrorAction Stop } catch {}
            }
    }
}
New-Item -ItemType Directory -Force -Path $releaseDir | Out-Null
$exe = Join-Path $releaseDir "PadMirror.exe"
$runtimeChecker = Join-Path $releaseDir "PadMirrorRuntimeCheck.exe"
Copy-Item $builtExe $exe -Force
Copy-Item $builtRuntimeChecker $runtimeChecker -Force

Invoke-NativeProcess (Join-Path $QtRoot "bin\windeployqt.exe") @(
    "--release",
    "--no-translations",
    "--verbose", "0",
    "--qmldir", (Join-Path $repoRoot "ui"),
    $exe
) "windeployqt failed"

foreach ($runtimeDll in $msvcRuntimeDlls) {
    $runtimePath = Join-Path $compilerDir $runtimeDll
    Require-Path $runtimePath "MSVC runtime $runtimeDll"
    Copy-Item $runtimePath $releaseDir -Force
}

Copy-Item (Join-Path $gstreamerBin "*.dll") $releaseDir -Force

# AAC-ELD (AirPlay screen audio) is decoded in-process by libfdk-aac, loaded at
# runtime with LoadLibrary. avdec_aac cannot decode AAC-ELD (AOT-39), so this
# DLL is required for AirPlay audio and must ship beside PadMirror.exe.
$fdkAacDll = First-ExistingPath @(
    (Join-Path $ToolsRoot "msys64\ucrt64\bin\libfdk-aac-2.dll"),
    (Join-Path $ToolsRoot "msys64\mingw64\bin\libfdk-aac-2.dll")
)
if ($fdkAacDll) {
    Copy-Item $fdkAacDll $releaseDir -Force
} else {
    Write-Warning "libfdk-aac-2.dll not found under $ToolsRoot; AirPlay AAC-ELD audio will be unavailable at runtime."
}
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
    foreach ($runtimeDll in $msvcRuntimeDlls) {
        Copy-Item (Join-Path $compilerDir $runtimeDll) $scannerTarget -Force
    }
}

$dependenciesDir = Join-Path $releaseDir "dependencies"
New-Item -ItemType Directory -Force -Path $dependenciesDir | Out-Null
Copy-Item (Join-Path $PSScriptRoot "repair-runtime.ps1") $dependenciesDir -Force

New-Item -ItemType Directory -Force -Path $downloadDir | Out-Null
$vcRedist = Join-Path $downloadDir "VC_redist.x64.exe"
Download-MicrosoftSigned "https://aka.ms/vs/17/release/vc_redist.x64.exe" $vcRedist
Copy-Item $vcRedist $dependenciesDir -Force

# AirPlay (in-process, gated to M6/M7) and USB (Apple user-mode channel, gated
# to M2) do not ship any external process, driver helper, firewall rule, or
# UxPlay bundle. Nothing is staged for them here until those milestones land.

$licenseTarget = Join-Path $releaseDir "licenses\PadMirror"
New-Item -ItemType Directory -Force -Path $licenseTarget | Out-Null
Copy-Item (Join-Path $repoRoot "LICENSES\THIRD_PARTY.md") $licenseTarget -Force
Copy-Item (Join-Path $repoRoot "LICENSE") $licenseTarget -Force

$runtimeReport = Join-Path $env:TEMP "padmirror-runtime-check.txt"
$runtimeRegistry = Join-Path $env:TEMP ("padmirror-runtime-registry-{0}.bin" -f [Guid]::NewGuid().ToString("N"))
if (Test-Path $runtimeReport) { Remove-Item $runtimeReport -Force }
$env:PADMIRROR_GST_REGISTRY = $runtimeRegistry
try {
    Invoke-NativeProcess $runtimeChecker @("--report=$runtimeReport") `
        "Packaged runtime check failed"
} finally {
    Remove-Item Env:PADMIRROR_GST_REGISTRY -ErrorAction SilentlyContinue
    Remove-Item $runtimeRegistry -Force -ErrorAction SilentlyContinue
}
if (-not (Test-Path $runtimeReport)) {
    $details = if (Test-Path $runtimeReport) { Get-Content $runtimeReport -Raw } else { "No report was produced." }
    throw "Packaged runtime check failed:`n$details"
}
Assert-NoForbiddenPayload $releaseDir

New-Item -ItemType Directory -Force -Path $distDir | Out-Null
$portableDir = Join-Path $buildDir "portable-package"
if (Test-Path $portableDir) {
    try {
        Remove-Item $portableDir -Recurse -Force -ErrorAction Stop
    } catch {}
}
Copy-Item $releaseDir $portableDir -Recurse -Force
$portableZip = Join-Path $distDir "PadMirror-portable.zip"
if (Test-Path $portableZip) {
    try {
        Remove-Item $portableZip -Force -ErrorAction Stop
    } catch {
        $portableZip = Join-Path $distDir ("PadMirror-portable-{0}.zip" -f (Get-Date -Format "yyyyMMdd-HHmmss"))
    }
}
Compress-Archive -Path (Join-Path $portableDir "*") -DestinationPath $portableZip -CompressionLevel Optimal

if (-not $SkipInstaller) {
    $iscc = Find-InnoCompiler
    Require-Path $iscc "Inno Setup compiler"
    $installerScript = Join-Path $repoRoot "installer\PadMirror.iss"
    $isccArguments = @(
        "/DReleaseDir=$releaseDir",
        "/DDistDir=$distDir",
        $installerScript
    )
    Invoke-NativeProcess $iscc $isccArguments "Inno Setup packaging failed"
}

$artifacts = @($exe, $runtimeChecker, $portableZip)
if (-not $SkipInstaller) { $artifacts += (Join-Path $distDir "PadMirrorSetup.exe") }
$checksumLines = foreach ($artifact in $artifacts) {
    Require-Path $artifact "release artifact"
    $hash = (Get-FileHashWithRetry $artifact).Hash.ToLowerInvariant()
    $relativeName = if ($artifact.StartsWith($releaseDir, [StringComparison]::OrdinalIgnoreCase)) {
        $artifact.Substring($releaseDir.Length).TrimStart('\', '/') -replace '\\', '/'
    } else {
        Split-Path -Leaf $artifact
    }
    "$hash  $relativeName"
}
$checksumLines | Set-Content (Join-Path $distDir "SHA256SUMS.txt") -Encoding ASCII
Write-Host "Stage integrity manifest: $(Join-Path $distDir 'SHA256SUMS.txt')"

Write-Host "PadMirror executable: $exe"
Write-Host "Portable package: $portableZip"
if (-not $SkipInstaller) { Write-Host "Installer: $(Join-Path $distDir 'PadMirrorSetup.exe')" }
