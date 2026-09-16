# One-time administrative setup for the fixed-purpose Pulse efficiency broker.
# Copyright (c) 2026 Peter Kosanyi. All rights reserved.
#Requires -RunAsAdministrator
[CmdletBinding()]
param([string]$SourceExe = (Join-Path (Split-Path $PSScriptRoot -Parent) 'Pulse.exe'))
$ErrorActionPreference = 'Stop'
$source = (Resolve-Path -LiteralPath $SourceExe).Path
if ([IO.Path]::GetFileName($source) -ne 'Pulse.exe') { throw 'Expected a built Pulse.exe' }
if ((Get-Item -LiteralPath $source).VersionInfo.ProductVersion -ne '1.3.0-efficiency-preview') { throw 'Unexpected Pulse version' }
$sid = [Security.Principal.WindowsIdentity]::GetCurrent().User.Value
$brokerDir = Join-Path $env:ProgramFiles 'Pulse\Efficiency'
$broker = Join-Path $brokerDir 'PulseBroker.exe'
$taskName = 'Pulse Efficiency ' + $sid
foreach ($path in @((Join-Path $env:ProgramFiles 'Pulse'), $brokerDir)) {
    if (Test-Path -LiteralPath $path) {
        if ((Get-Item -LiteralPath $path).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Refusing a redirected broker directory' }
    } else { New-Item -ItemType Directory -Path $path | Out-Null }
}
$acl = New-Object Security.AccessControl.DirectorySecurity
$acl.SetAccessRuleProtection($true, $false)
$admins = New-Object Security.Principal.SecurityIdentifier('S-1-5-32-544')
$acl.SetOwner($admins)
foreach ($entry in @(@('S-1-5-18','FullControl'), @('S-1-5-32-544','FullControl'), @('S-1-5-32-545','ReadAndExecute'))) {
    $identity = New-Object Security.Principal.SecurityIdentifier($entry[0])
    $rule = New-Object Security.AccessControl.FileSystemAccessRule($identity, $entry[1], 'ContainerInherit,ObjectInherit', 'None', 'Allow')
    $acl.AddAccessRule($rule)
}
Set-Acl -LiteralPath $brokerDir -AclObject $acl
$existing = Get-ScheduledTask -TaskName $taskName -ErrorAction SilentlyContinue
if ($existing -and ($existing.Actions.Execute -ne $broker)) { throw 'Task name is already used by another action' }
if ($existing -and $existing.State -eq 'Running') { throw 'Exit Pulse and let its helper restore before updating the broker' }
if ((Test-Path -LiteralPath $broker) -and ((Get-Item -LiteralPath $broker).Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw 'Refusing a redirected broker executable' }
Copy-Item -LiteralPath $source -Destination $broker -Force
$fileAcl = New-Object Security.AccessControl.FileSecurity
$fileAcl.SetAccessRuleProtection($true, $false)
$fileAcl.SetOwner($admins)
foreach ($entry in @(@('S-1-5-18','FullControl'), @('S-1-5-32-544','FullControl'), @('S-1-5-32-545','ReadAndExecute'))) {
    $identity = New-Object Security.Principal.SecurityIdentifier($entry[0])
    $fileAcl.AddAccessRule((New-Object Security.AccessControl.FileSystemAccessRule($identity, $entry[1], 'Allow')))
}
Set-Acl -LiteralPath $broker -AclObject $fileAcl
if ((Get-FileHash -LiteralPath $source).Hash -ne (Get-FileHash -LiteralPath $broker).Hash) { throw 'Broker copy verification failed' }
$action = New-ScheduledTaskAction -Execute $broker -Argument '--efficiency-helper $(Arg0) --parent $(Arg1) --owner $(Arg2)'
$principal = New-ScheduledTaskPrincipal -UserId $sid -LogonType Interactive -RunLevel Highest
$settings = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries -ExecutionTimeLimit ([TimeSpan]::Zero) -MultipleInstances Parallel
Register-ScheduledTask -TaskName $taskName -Action $action -Principal $principal -Settings $settings -Description 'Pulse fixed-purpose Update/Defender preference broker; starts on demand, no periodic trigger.' -Force | Out-Null
$scheduler = New-Object -ComObject 'Schedule.Service'
$scheduler.Connect()
$task = $scheduler.GetFolder('\').GetTask($taskName)
# The initiating account may read/run the fixed task, not rewrite its elevated action.
$task.SetSecurityDescriptor(('O:BAG:BAD:P(A;;FA;;;SY)(A;;FA;;;BA)(A;;GRGX;;;' + $sid + ')'), 0)
Write-Output ('Installed broker: ' + $broker)
Write-Output ('SHA256: ' + (Get-FileHash -LiteralPath $broker).Hash)
Write-Output ('On-demand task: ' + $taskName)
Write-Output 'No Update or Defender preference has been changed by this installer.'
