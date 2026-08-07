param([switch]$Elevated)

$ErrorActionPreference = "Stop"
$expectedHash = "91F6F695E1E13C656024E6D3B55620BF08D8835EF05EE0496935BA6BB62466A5"
$installer = Join-Path $PSScriptRoot "UsbDk_1.0.22_x64.msi"

function Test-Administrator {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = [Security.Principal.WindowsPrincipal]$identity
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

if (Get-Service UsbDk -ErrorAction SilentlyContinue) {
    exit 0
}
if (-not (Test-Path $installer)) {
    throw "The bundled UsbDk installer is missing."
}
if ((Get-FileHash $installer -Algorithm SHA256).Hash -ne $expectedHash) {
    throw "The UsbDk installer checksum is invalid."
}
$signature = Get-AuthenticodeSignature $installer
if ($signature.Status -ne "Valid" -or $signature.SignerCertificate.Subject -notmatch "Red Hat") {
    throw "The UsbDk installer signature is invalid."
}

if (-not (Test-Administrator)) {
    if ($Elevated) { throw "Administrator permission was not granted." }
    $arguments = @(
        "-NoProfile",
        "-ExecutionPolicy", "Bypass",
        "-File", "`"$PSCommandPath`"",
        "-Elevated"
    )
    try {
        $process = Start-Process powershell.exe -Verb RunAs -ArgumentList $arguments -Wait -PassThru
        exit $process.ExitCode
    } catch {
        throw "Administrator permission is required to install the USB capture driver."
    }
}

$process = Start-Process msiexec.exe -ArgumentList @(
    "/i", "`"$installer`"", "/qn", "/norestart"
) -Wait -PassThru
if ($process.ExitCode -notin @(0, 3010)) {
    throw "UsbDk installation failed with exit code $($process.ExitCode)."
}
if (-not (Get-Service UsbDk -ErrorAction SilentlyContinue)) {
    throw "UsbDk was installed but its service is not available. Restart Windows once."
}

Write-Output "UsbDk capture driver is ready."
