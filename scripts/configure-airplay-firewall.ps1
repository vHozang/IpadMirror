# Adds Windows Firewall inbound allow rules for PadMirror's in-process AirPlay
# receiver: mDNS discovery (UDP 5353) plus the RTSP/AirPlay TCP+UDP listeners.
# Program-scoped so only PadMirror.exe is allowed, on Private+Domain profiles.
param(
    [string]$ExePath = (Join-Path (Split-Path -Parent $PSScriptRoot) "PadMirror.exe")
)
$ErrorActionPreference = "Stop"

if (-not (Test-Path $ExePath)) {
    Write-Error "PadMirror.exe not found at $ExePath"
    exit 1
}
$ExePath = (Resolve-Path $ExePath).Path
Write-Host "Configuring firewall for: $ExePath"

$rules = @(
    @{ Name = "PadMirror AirPlay (UDP In)";  Protocol = "UDP" },
    @{ Name = "PadMirror AirPlay (TCP In)";  Protocol = "TCP" }
)

Get-NetFirewallRule -DisplayName "*PadMirror*" -ErrorAction SilentlyContinue |
    Remove-NetFirewallRule -ErrorAction SilentlyContinue

foreach ($r in $rules) {
    New-NetFirewallRule `
        -DisplayName $r.Name `
        -Direction Inbound `
        -Action Allow `
        -Program $ExePath `
        -Protocol $r.Protocol `
        -Profile Domain,Private `
        -Enabled True | Out-Null
    Write-Host "  added: $($r.Name) [$($r.Protocol)]"
}

Write-Host "Done. Current PadMirror rules:"
Get-NetFirewallRule -DisplayName "PadMirror*" |
    Select-Object DisplayName, Enabled, Direction, Action, Profile |
    Format-Table -AutoSize
