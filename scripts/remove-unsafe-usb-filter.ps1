param([switch]$Elevated)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest
$global:LASTEXITCODE = 0

function Test-Administrator {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = [Security.Principal.WindowsPrincipal]$identity
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Get-AppleUsbRegistryPaths {
    $usbRoot = "HKLM:\SYSTEM\CurrentControlSet\Enum\USB"
    $paths = foreach ($deviceKey in Get-ChildItem $usbRoot -ErrorAction SilentlyContinue) {
        if ($deviceKey.PSChildName -notmatch '^VID_05AC&PID_12') { continue }
        Get-ChildItem $deviceKey.PSPath -ErrorAction SilentlyContinue |
            Select-Object -ExpandProperty PSPath
    }
    return @($paths)
}

function Get-PresentAppleCompositeIds {
    return @(Get-PnpDevice -PresentOnly -ErrorAction SilentlyContinue | Where-Object {
        if ($_.InstanceId -notmatch '^USB\\VID_05AC&PID_12' -or
            $_.InstanceId -match '&MI_') {
            return $false
        }
        $service = Get-PnpDeviceProperty -InstanceId $_.InstanceId -KeyName DEVPKEY_Device_Service `
            -ErrorAction SilentlyContinue
        return $null -ne $service -and $service.Data -eq "usbccgp"
    } | Select-Object -ExpandProperty InstanceId)
}

function Remove-FilterValue([string]$Path, [string]$PropertyName) {
    $item = Get-ItemProperty -Path $Path -Name $PropertyName -ErrorAction SilentlyContinue
    if ($null -eq $item) { return $false }
    $property = $item.PSObject.Properties[$PropertyName]
    if ($null -eq $property) { return $false }

    $filters = @($property.Value)
    if (-not ($filters | Where-Object { $_ -ieq "libusb0" })) { return $false }

    $remaining = @($filters | Where-Object { $_ -ine "libusb0" })
    if ($remaining.Count -eq 0) {
        Remove-ItemProperty -Path $Path -Name $PropertyName -ErrorAction Stop
    } else {
        Set-ItemProperty -Path $Path -Name $PropertyName -Value $remaining -Type MultiString
    }
    return $true
}

function Test-FilterValue([string]$Path, [string]$PropertyName) {
    $item = Get-ItemProperty -Path $Path -Name $PropertyName -ErrorAction SilentlyContinue
    if ($null -eq $item) { return $false }
    $property = $item.PSObject.Properties[$PropertyName]
    return $null -ne $property -and @($property.Value) -contains "libusb0"
}

function Remove-UsbDk {
    $service = Get-Service UsbDk -ErrorAction SilentlyContinue
    if (-not $service) { return $false }

    & (Join-Path $env:SystemRoot "System32\sc.exe") config UsbDk start= disabled | Out-Null
    if ($service.Status -ne "Stopped") {
        $stopOutput = & (Join-Path $env:SystemRoot "System32\sc.exe") stop UsbDk 2>&1
        $stopExitCode = $LASTEXITCODE
        $stopOutput | Write-Output
        Write-Output "UsbDk stop exit code: $stopExitCode"
        for ($attempt = 0; $attempt -lt 20; ++$attempt) {
            Start-Sleep -Milliseconds 250
            $service = Get-Service UsbDk -ErrorAction SilentlyContinue
            if (-not $service -or $service.Status -eq "Stopped") { break }
        }
    }

    $productCodes = @(
        "HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\*",
        "HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\*"
    ) | ForEach-Object {
        Get-ItemProperty $_ -ErrorAction SilentlyContinue
    } | Where-Object {
        $displayName = $_.PSObject.Properties["DisplayName"]
        $childName = $_.PSObject.Properties["PSChildName"]
        $null -ne $displayName -and $null -ne $childName -and
            $displayName.Value -like "UsbDk*" -and
            $childName.Value -match '^\{[0-9A-Fa-f-]+\}$'
    } | Select-Object -ExpandProperty PSChildName -Unique

    foreach ($productCode in $productCodes) {
        $process = Start-Process msiexec.exe -ArgumentList @(
            "/x", $productCode, "/qn", "/norestart"
        ) -Wait -PassThru
        if ($process.ExitCode -notin @(0, 1605, 1614, 3010)) {
            throw "UsbDk uninstall failed with exit code $($process.ExitCode)."
        }
    }

    if (Get-Service UsbDk -ErrorAction SilentlyContinue) {
        & (Join-Path $env:SystemRoot "System32\sc.exe") delete UsbDk | Out-Null
        if ($LASTEXITCODE -notin @(0, 1060)) {
            throw "The UsbDk service could not be removed (exit $LASTEXITCODE)."
        }
    }
    return $true
}

if (-not (Test-Administrator)) {
    if ($Elevated) { throw "Administrator permission was not granted." }
    $arguments = @(
        "-NoProfile",
        "-ExecutionPolicy", "Bypass",
        "-File", "`"$PSCommandPath`"",
        "-Elevated"
    )
    $process = Start-Process powershell.exe -Verb RunAs -ArgumentList $arguments -Wait -PassThru
    exit $process.ExitCode
}

$usbDkRemoved = Remove-UsbDk
$restartRequired = $false

$presentAppleDevices = Get-PresentAppleCompositeIds
$changed = 0
foreach ($path in Get-AppleUsbRegistryPaths) {
    if (Remove-FilterValue $path "UpperFilters") { ++$changed }
    if (Remove-FilterValue $path "LowerFilters") { ++$changed }
}

$remainingUses = foreach ($path in Get-AppleUsbRegistryPaths) {
    if ((Test-FilterValue $path "UpperFilters") -or
        (Test-FilterValue $path "LowerFilters")) {
        $path
    }
}
if (@($remainingUses).Count -ne 0) {
    throw "The unsafe libusb0 filter could not be removed from every Apple USB device."
}

foreach ($instanceId in $presentAppleDevices) {
    & (Join-Path $env:SystemRoot "System32\pnputil.exe") /restart-device $instanceId | Out-Null
    if ($LASTEXITCODE -ne 0) {
        $restartRequired = $true
    }
}

$allFilterUses = @(
    Get-ChildItem "HKLM:\SYSTEM\CurrentControlSet\Enum\USB" -Recurse -ErrorAction SilentlyContinue |
        Where-Object {
            (Test-FilterValue $_.PSPath "UpperFilters") -or
            (Test-FilterValue $_.PSPath "LowerFilters")
        }
)
if ($allFilterUses.Count -ne 0) {
    Write-Output "Removed $changed unsafe libusb0 filter entries from Apple USB devices."
    Write-Output "The shared libusb0 service was kept because another USB device still uses it."
    exit 0
}

$service = Get-Service libusb0 -ErrorAction SilentlyContinue
if ($service) {
    if ($service.Status -ne "Stopped") {
        Stop-Service libusb0 -Force -ErrorAction Stop
    }
    & (Join-Path $env:SystemRoot "System32\sc.exe") delete libusb0 | Out-Null
    if ($LASTEXITCODE -notin @(0, 1060)) {
        throw "The obsolete libusb0 service could not be removed (exit $LASTEXITCODE)."
    }
}

$driver = Join-Path $env:SystemRoot "System32\drivers\libusb0.sys"
if (Test-Path $driver) {
    $disabledDriver = "$driver.padmirror-disabled"
    Move-Item $driver $disabledDriver -Force
}

Write-Output "Removed $changed unsafe libusb0 filter entries from Apple USB devices."
if ($usbDkRemoved) {
    Write-Output "Removed the UsbDk kernel capture driver that caused WDF_VIOLATION."
}
if ($restartRequired) {
    Write-Output "Windows must restart once to unload the disabled USB driver."
    exit 3010
}
