param(
    [Parameter(Mandatory = $true)]
    [string]$UxPlayPath,
    [switch]$Elevated
)

$ErrorActionPreference = "Stop"
$ruleNames = @(
    "PadMirror-UxPlay-mDNS",
    "PadMirror-UxPlay-UDP",
    "PadMirror-UxPlay-TCP"
)

function Test-Administrator {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = [Security.Principal.WindowsPrincipal]$identity
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Test-CurrentRules {
    foreach ($name in $ruleNames) {
        $rule = Get-NetFirewallRule -Name $name -ErrorAction SilentlyContinue
        if ($null -eq $rule -or $rule.Enabled -ne "True" -or $rule.Action -ne "Allow") {
            return $false
        }
        $application = $rule | Get-NetFirewallApplicationFilter
        if ($application.Program -ne $UxPlayPath) {
            return $false
        }
    }
    return $true
}

$UxPlayPath = (Resolve-Path $UxPlayPath).Path
if (Test-CurrentRules) {
    exit 0
}

if (-not (Test-Administrator)) {
    if ($Elevated) { throw "Administrator permission was not granted." }
    $arguments = @(
        "-NoProfile",
        "-ExecutionPolicy", "Bypass",
        "-File", "`"$PSCommandPath`"",
        "-UxPlayPath", "`"$UxPlayPath`"",
        "-Elevated"
    )
    try {
        $process = Start-Process powershell.exe -Verb RunAs -ArgumentList $arguments -Wait -PassThru
        exit $process.ExitCode
    } catch {
        throw "Administrator permission is required to allow AirPlay through Windows Firewall."
    }
}

foreach ($name in $ruleNames) {
    Remove-NetFirewallRule -Name $name -ErrorAction SilentlyContinue
}

New-NetFirewallRule -Name $ruleNames[0] -DisplayName "PadMirror UxPlay mDNS" `
    -Direction Inbound -Action Allow -Enabled True -Profile Any `
    -Program $UxPlayPath -Protocol UDP -LocalPort 5353 | Out-Null
New-NetFirewallRule -Name $ruleNames[1] -DisplayName "PadMirror UxPlay AirPlay UDP" `
    -Direction Inbound -Action Allow -Enabled True -Profile Any `
    -Program $UxPlayPath -Protocol UDP -LocalPort 7100-7102 | Out-Null
New-NetFirewallRule -Name $ruleNames[2] -DisplayName "PadMirror UxPlay AirPlay TCP" `
    -Direction Inbound -Action Allow -Enabled True -Profile Any `
    -Program $UxPlayPath -Protocol TCP -LocalPort 7100-7102 | Out-Null

Write-Output "PadMirror AirPlay firewall rules are ready."
