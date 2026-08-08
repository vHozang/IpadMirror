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
$firewallRegistry = "HKLM:\SYSTEM\CurrentControlSet\Services\SharedAccess\Parameters\FirewallPolicy\FirewallRules"

function Test-Administrator {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = [Security.Principal.WindowsPrincipal]$identity
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Get-ProgramRuleProperties {
    $properties = (Get-ItemProperty $firewallRegistry -ErrorAction SilentlyContinue).PSObject.Properties
    $programToken = ("App=$UxPlayPath").ToLowerInvariant()
    return @($properties | Where-Object {
        $_.Name -notlike "PS*" -and
        $_.Value.ToString().ToLowerInvariant().Contains($programToken) -and
        $_.Value -match "Dir=In"
    })
}

function Test-CurrentRules {
    $programRules = Get-ProgramRuleProperties
    if ($programRules | Where-Object { $_.Value -match "Action=Block" }) {
        return $false
    }

    $expected = @(
        @{ DisplayName = "PadMirror UxPlay mDNS"; Protocol = "Protocol=17"; Port = "LPort=5353" },
        @{ DisplayName = "PadMirror UxPlay AirPlay UDP"; Protocol = "Protocol=17"; Port = "7100-7102" },
        @{ DisplayName = "PadMirror UxPlay AirPlay TCP"; Protocol = "Protocol=6"; Port = "7100-7102" }
    )
    foreach ($item in $expected) {
        $matched = $programRules | Where-Object {
            $_.Value -match "Action=Allow" -and
            $_.Value -match "Active=TRUE" -and
            $_.Value -match "Dir=In" -and
            $_.Value -like "*Name=$($item.DisplayName)|*" -and
            $_.Value -like "*$($item.Protocol)*" -and
            $_.Value -like "*$($item.Port)*"
        }
        if (-not $matched) { return $false }
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
    Remove-NetFirewallRule -PolicyStore PersistentStore -Name $name -ErrorAction SilentlyContinue
}

# A previous Windows firewall prompt may have created a Public-profile block
# rule. Explicit block rules override PadMirror's allow rules, so remove every
# old inbound rule for this bundled UxPlay path before recreating scoped rules.
& netsh.exe advfirewall firewall delete rule name=all dir=in "program=$UxPlayPath" | Out-Null
foreach ($property in Get-ProgramRuleProperties) {
    Remove-NetFirewallRule -PolicyStore PersistentStore -Name $property.Name -ErrorAction SilentlyContinue
}
foreach ($property in Get-ProgramRuleProperties) {
    Remove-ItemProperty -Path $firewallRegistry -Name $property.Name -ErrorAction Stop
}

New-NetFirewallRule -PolicyStore PersistentStore -Name $ruleNames[0] -DisplayName "PadMirror UxPlay mDNS" `
    -Direction Inbound -Action Allow -Enabled True -Profile Any `
    -Program $UxPlayPath -Protocol UDP -LocalPort 5353 | Out-Null
New-NetFirewallRule -PolicyStore PersistentStore -Name $ruleNames[1] -DisplayName "PadMirror UxPlay AirPlay UDP" `
    -Direction Inbound -Action Allow -Enabled True -Profile Any `
    -Program $UxPlayPath -Protocol UDP -LocalPort 7100-7102 | Out-Null
New-NetFirewallRule -PolicyStore PersistentStore -Name $ruleNames[2] -DisplayName "PadMirror UxPlay AirPlay TCP" `
    -Direction Inbound -Action Allow -Enabled True -Profile Any `
    -Program $UxPlayPath -Protocol TCP -LocalPort 7100-7102 | Out-Null

Start-Sleep -Milliseconds 300
if (-not (Test-CurrentRules)) {
    throw "Windows Firewall did not keep the PadMirror AirPlay allow rules."
}

Write-Output "PadMirror AirPlay firewall rules are ready."
