param([switch]$Elevated)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$cleanupScript = Join-Path $PSScriptRoot "remove-unsafe-usb-filter.ps1"

function Test-Administrator {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = [Security.Principal.WindowsPrincipal]$identity
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Get-ServiceState([string]$Name) {
    return Get-Service -Name $Name -ErrorAction SilentlyContinue
}

function Test-UnsafeUsbFilter {
    $usbRoot = "HKLM:\SYSTEM\CurrentControlSet\Enum\USB"
    foreach ($deviceKey in Get-ChildItem $usbRoot -ErrorAction SilentlyContinue) {
        if ($deviceKey.PSChildName -notmatch '^VID_05AC&PID_12') { continue }
        foreach ($instance in Get-ChildItem $deviceKey.PSPath -ErrorAction SilentlyContinue) {
            foreach ($propertyName in @("UpperFilters", "LowerFilters")) {
                $item = Get-ItemProperty $instance.PSPath -Name $propertyName -ErrorAction SilentlyContinue
                if ($null -eq $item) { continue }
                $property = $item.PSObject.Properties[$propertyName]
                if ($null -ne $property -and @($property.Value) -contains "libusb0") {
                    return $true
                }
            }
        }
    }
    return $false
}

function Install-AppleDevices {
    $wingetCommand = Get-Command winget.exe -ErrorAction SilentlyContinue
    $winget = if ($wingetCommand) { $wingetCommand.Source } else { $null }
    if ([string]::IsNullOrWhiteSpace($winget)) {
        throw "Apple Devices is required, but winget is unavailable. Install Apple Devices from Microsoft Store."
    }
    $process = Start-Process -FilePath $winget -ArgumentList @(
        "install", "--id", "9NP83LWLPZ9K", "--exact", "--source", "msstore",
        "--silent", "--disable-interactivity", "--accept-package-agreements",
        "--accept-source-agreements"
    ) -Wait -PassThru -NoNewWindow
    if ($process.ExitCode -ne 0) {
        throw "Apple Devices installation failed with exit code $($process.ExitCode)."
    }
}

function Wait-ForService([string]$Name, [int]$Seconds) {
    for ($attempt = 0; $attempt -lt $Seconds; ++$attempt) {
        $service = Get-ServiceState $Name
        if ($service) { return $service }
        Start-Sleep -Seconds 1
    }
    return $null
}

$appleService = Get-ServiceState "Apple Mobile Device Service"
if (-not $appleService -and -not $Elevated) {
    Install-AppleDevices
    $appleService = Wait-ForService "Apple Mobile Device Service" 30
}

$unsafeDriverPresent = $null -ne (Get-ServiceState "UsbDk") -or (Test-UnsafeUsbFilter)
$needsElevation = $unsafeDriverPresent -or -not $appleService -or $appleService.Status -ne "Running"
if (-not $needsElevation) {
    Write-Output "Safe Apple USB support is ready."
    exit 0
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
        throw "Administrator permission is required to remove unsafe USB drivers."
    }
}

if (-not (Test-Path $cleanupScript)) {
    throw "The unsafe USB driver cleanup script is missing."
}
& $cleanupScript -Elevated

$appleService = Get-ServiceState "Apple Mobile Device Service"
if (-not $appleService) {
    throw "Apple Mobile Device Service is unavailable. Restart Windows, then reinstall Apple Devices."
}
if ($appleService.Status -ne "Running") {
    Start-Service -Name "Apple Mobile Device Service"
}
if ((Get-ServiceState "UsbDk") -or (Test-UnsafeUsbFilter)) {
    throw "Unsafe USB drivers are pending removal. Restart Windows before using USB mode."
}

Write-Output "Safe Apple USB support is ready. UsbDk and libusb0 are not used."
