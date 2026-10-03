# Installs only the user-mode service and tray panel. Kernel driver installation
# requires a signed, qualified package and is deliberately a separate operation.
[CmdletBinding(SupportsShouldProcess=$true)]
param(
    [Parameter(Mandatory=$true)][string]$Package,
    [string]$Destination = "$env:ProgramFiles/Rey Audio Driver",
    [string]$ResultFile = '',
    [switch]$Remove
)
$ErrorActionPreference = 'Stop'
$serviceName = 'ReyAudioService'
$configurationRoot = Join-Path $env:ProgramData 'ReyAudio'
$configuration = Join-Path $configurationRoot 'service.ini'
$packageRoot = [IO.Path]::GetFullPath($Package)
$installRoot = [IO.Path]::GetFullPath($Destination)
if ($installRoot.TrimEnd('\','/') -eq [IO.Path]::GetPathRoot($installRoot).TrimEnd('\','/')) { throw 'A drive root is not an installation directory.' }
function Write-Result($result) {
    if ($ResultFile) { $result | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $ResultFile -Encoding UTF8 }
}
try {
    if (!$PSCmdlet.ShouldProcess($installRoot, $(if ($Remove) {'Remove Rey Audio service and autorun'} else {'Install Rey Audio service and tray panel'}))) { return }
    if (![Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) { throw 'Run this installer from an Administrator PowerShell.' }
    $existing = Get-Service -Name $serviceName -ErrorAction SilentlyContinue
    if ($Remove) {
        if ($existing) { Stop-Service -Name $serviceName -Force; & sc.exe delete $serviceName | Out-Null; if ($LASTEXITCODE) { throw 'Service removal failed.' } }
        Remove-ItemProperty -LiteralPath 'HKLM:/Software/Microsoft/Windows/CurrentVersion/Run' -Name 'ReyAudioControl' -ErrorAction SilentlyContinue
        Remove-NetFirewallRule -Name 'ReyAudio-LAN' -ErrorAction SilentlyContinue
        # Keep settings and installed files for rollback; no recursive deletion.
        Write-Result @{ installed=$false; service_removed=$true; settings_preserved=$true }
        return
    }
    $manifest = Get-Content -LiteralPath (Join-Path $packageRoot 'manifest.json') -Raw | ConvertFrom-Json
    foreach ($name in @('ReyAudioService.exe','ReyAudioControl.exe','ReyAudioProbe.exe')) {
        $source = Join-Path $packageRoot ('bin/'+$name)
        $expected = $manifest.payload_sha256.('bin/'+$name)
        if (!$expected -or (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash.ToLowerInvariant() -ne $expected) { throw ('Package hash mismatch: '+$name) }
    }
    if ($existing) {
        $current = Get-CimInstance Win32_Service -Filter "Name='ReyAudioService'"
        if (!$current.PathName.StartsWith('"'+(Join-Path $installRoot 'ReyAudioService.exe')+'"', [StringComparison]::OrdinalIgnoreCase)) { throw 'Existing service belongs to another installation. Inspect it before replacement.' }
        Stop-Service -Name $serviceName -Force
    }
    [IO.Directory]::CreateDirectory($installRoot) | Out-Null
    [IO.Directory]::CreateDirectory($configurationRoot) | Out-Null
    foreach ($protectedRoot in @($installRoot,$configurationRoot)) {
        $attributes = [IO.File]::GetAttributes($protectedRoot)
        if (($attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw 'Installation/configuration directory cannot be a reparse point.' }
        $acl = [Security.AccessControl.DirectorySecurity]::new()
        $acl.SetAccessRuleProtection($true,$false)
        foreach ($identity in @('S-1-5-18','S-1-5-32-544')) {
            $acl.AddAccessRule([Security.AccessControl.FileSystemAccessRule]::new([Security.Principal.SecurityIdentifier]::new($identity), 'FullControl', 'ContainerInherit,ObjectInherit', 'None', 'Allow'))
        }
        $acl.AddAccessRule([Security.AccessControl.FileSystemAccessRule]::new([Security.Principal.SecurityIdentifier]::new('S-1-5-32-545'), 'ReadAndExecute', 'ContainerInherit,ObjectInherit', 'None', 'Allow'))
        Set-Acl -LiteralPath $protectedRoot -AclObject $acl
    }
    foreach ($name in @('ReyAudioService.exe','ReyAudioControl.exe','ReyAudioProbe.exe')) { Copy-Item -LiteralPath (Join-Path $packageRoot ('bin/'+$name)) -Destination (Join-Path $installRoot $name) -Force }
    if (!(Test-Path -LiteralPath $configuration)) { Copy-Item -LiteralPath (Join-Path $packageRoot 'service.example.ini') -Destination $configuration }
    $command = '"'+(Join-Path $installRoot 'ReyAudioService.exe')+'" --service --config "'+$configuration+'"'
    if (!$existing) { New-Service -Name $serviceName -BinaryPathName $command -DisplayName 'Rey Audio Driver' -Description 'USB detection and AoIP audio transport for Rey Audio devices.' -StartupType Automatic | Out-Null }
    else { & sc.exe config $serviceName binPath= $command start= auto | Out-Null; if ($LASTEXITCODE) { throw 'Service configuration failed.' } }
    & sc.exe failure $serviceName reset= 86400 actions= restart/3000/restart/5000/restart/10000 | Out-Null
    if ($LASTEXITCODE) { throw 'Service recovery configuration failed.' }
    if (!(Get-NetFirewallRule -Name 'ReyAudio-LAN' -ErrorAction SilentlyContinue)) {
        New-NetFirewallRule -Name 'ReyAudio-LAN' -DisplayName 'Rey Audio LAN transport' -Direction Inbound -Action Allow -Profile Private -Protocol UDP -LocalPort 50021 -RemoteAddress LocalSubnet -Program (Join-Path $installRoot 'ReyAudioService.exe') | Out-Null
    }
    New-ItemProperty -LiteralPath 'HKLM:/Software/Microsoft/Windows/CurrentVersion/Run' -Name 'ReyAudioControl' -PropertyType String -Value ('"'+(Join-Path $installRoot 'ReyAudioControl.exe')+'" --tray') -Force | Out-Null
    Start-Service -Name $serviceName
    (Get-Service -Name $serviceName).WaitForStatus('Running',[TimeSpan]::FromSeconds(10))
    Write-Result @{ installed=$true; service=$serviceName; start='Automatic'; path=$installRoot; configuration=$configuration; kernel_driver_installed=$false; tray_autorun=$true }
} catch {
    Write-Result @{ installed=$false; error=$_.Exception.Message }
    throw
}
