param(
    [Parameter(Mandatory=$true)][string]$Setup,
    [Parameter(Mandatory=$true)][string]$Bin,
    [Parameter(Mandatory=$true)][string]$Driver,
    [string]$Wix = '../.tools/wix3141',
    [string]$Output = 'build/setup-inspection'
)
$ErrorActionPreference = 'Stop'
function Require([bool]$Condition, [string]$Message) { if (!$Condition) { throw $Message } }
function Digest([string]$Path) { (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant() }
function ReadMsiRows([string]$Table, [string[]]$Columns) {
    $quoted = ($Columns | ForEach-Object { '`'+$_+'`' }) -join ', '
    $view = $database.OpenView(('SELECT '+$quoted+' FROM `'+$Table+'`'))
    $items = New-Object 'Collections.Generic.List[object]'
    try {
        $null = $view.Execute()
        while($record = $view.Fetch()) {
            $item = [ordered]@{}
            for($index = 0; $index -lt $Columns.Count; $index++) { $item[$Columns[$index]] = $record.StringData($index+1) }
            $null = $items.Add([pscustomobject]$item)
        }
    } finally { $null = $view.Close() }
    return $items.ToArray()
}
$setupRoot = [IO.Path]::GetFullPath($Setup)
$destination = [IO.Path]::GetFullPath($Output)
Require (!(Test-Path -LiteralPath $destination)) 'Use a new inspection directory.'
[IO.Directory]::CreateDirectory($destination) | Out-Null
$msi = Join-Path $setupRoot 'Rey-Audio-Driver-2.7.0-preview.1-x64.msi'
$exe = Join-Path $setupRoot 'Rey-Audio-Setup-2.7.0-preview.1-x64.exe'
$installer = New-Object -ComObject WindowsInstaller.Installer
$database = $installer.OpenDatabase($msi, 0)
$properties = @{}
foreach($row in (ReadMsiRows -Table 'Property' -Columns @('Property','Value'))) { $properties[$row.Property] = $row.Value }
Require ($properties['ProductVersion'] -eq '2.7.0') 'Unexpected MSI version.'
Require ($properties['ALLUSERS'] -eq '1') 'Installation must be per-machine.'
$allLaunch = @(ReadMsiRows -Table 'LaunchCondition' -Columns @('Condition','Description'))
$launch = @($allLaunch | Where-Object { $_.Condition -match 'NETFRAMEWORK48' })
Require ($launch.Count -eq 1) 'Unexpected launch conditions.'
# OpenPackage(options=1) prevents machine-state changes. AppSearch only reads
# the two authored registry searches; no custom/install action is executed.
$session = $installer.OpenPackage($msi, 1)
$null = $session.DoAction('AppSearch')
$actualNt = $session.Property('VersionNT64')
$actualFramework = $session.Property('NETFRAMEWORK48')
foreach($entry in $allLaunch) { Require ($session.EvaluateCondition($entry.Condition) -eq 1) 'Launch condition rejects this build PC.' }
$session.Property('VersionNT64') = '603'
$session.Property('NETFRAMEWORK48') = '#528040'
Require ($session.EvaluateCondition($launch[0].Condition) -eq 1) 'Compatible MSI OS value and minimum .NET must pass.'
$session.Property('NETFRAMEWORK48') = '#528039'
Require ($session.EvaluateCondition($launch[0].Condition) -eq 0) 'Older .NET must fail.'
$session.Property('NETFRAMEWORK48') = '#528040'
$session.Property('VersionNT64') = ''
Require ($session.EvaluateCondition($launch[0].Condition) -eq 0) 'Non-x64 must fail.'
$session.Property('Installed') = '1'
Require ($session.EvaluateCondition($launch[0].Condition) -eq 1) 'Uninstall must remain available.'
$files = @(ReadMsiRows -Table 'File' -Columns @('File','FileName','FileSize'))
Require ($files.Count -eq 7) 'Unexpected MSI payload count.'
Require (@($files | Where-Object { $_.FileName -match '\.ini$|\.ps1$' }).Count -eq 0) 'Configuration or installation scripts in MSI.'
$service = @(ReadMsiRows -Table 'ServiceInstall' -Columns @('Name','StartType','StartName','Arguments'))
Require ($service.Count -eq 1 -and $service[0].Name -eq 'ReyAudioService' -and $service[0].StartType -eq '2' -and $service[0].StartName -eq 'LocalSystem' -and $service[0].Arguments -eq '--service') 'Unexpected service installation.'
$firewall = @(ReadMsiRows -Table 'WixFirewallException' -Columns @('Name','RemoteAddresses','Port','Protocol','Program','Profile'))
Require ($firewall.Count -eq 1 -and $firewall[0].Name -eq 'Rey Audio LAN transport' -and $firewall[0].RemoteAddresses -eq 'LocalSubnet' -and $firewall[0].Port -eq '50021' -and $firewall[0].Protocol -eq '17' -and $firewall[0].Program -eq '[#ServiceExe]' -and $firewall[0].Profile -eq '2') 'Unexpected firewall scope.'
$sequence = @{}
foreach($row in (ReadMsiRows -Table 'InstallExecuteSequence' -Columns @('Action','Condition','Sequence'))) { $sequence[$row.Action] = $row }
Require ([int]$sequence.ReyCheck.Sequence -lt [int]$sequence.InstallInitialize.Sequence) 'Readiness check must precede mutation.'
Require ([int]$sequence.RemoveExistingProducts.Sequence -gt [int]$sequence.InstallInitialize.Sequence -and [int]$sequence.RemoveExistingProducts.Sequence -lt [int]$sequence.InstallFiles.Sequence) 'Upgrade must remain in transaction.'
Require ([int]$sequence.ReyRollback.Sequence -gt [int]$sequence.InstallFiles.Sequence -and [int]$sequence.ReyDriverInstall.Sequence -gt [int]$sequence.ReyRollback.Sequence -and [int]$sequence.ReyCommit.Sequence -gt [int]$sequence.ReyDriverInstall.Sequence) 'Unexpected driver action order.'
Require ([int]$sequence.ReyDriverRemove.Sequence -gt [int]$sequence.StopServices.Sequence -and [int]$sequence.ReyDriverRemove.Sequence -lt [int]$sequence.RemoveFiles.Sequence) 'Removal must stop service first and retain helper until removal.'
Require ($sequence.ReyDriverRemove.Condition -eq 'REMOVE="ALL" AND NOT UPGRADINGPRODUCTCODE') 'Upgrade must preserve the existing owned root.'
$actions = @(ReadMsiRows -Table 'CustomAction' -Columns @('Action','Type','Source','Target') | Where-Object { $_.Action -match '^Rey' })
foreach($name in @('ReyRollback','ReyDriverInstall','ReyCommit','ReyDriverRemove')) {
    $action = @($actions | Where-Object { $_.Action -eq $name })
    Require ($action.Count -eq 1 -and ([int]$action[0].Type -band 1024) -ne 0 -and ([int]$action[0].Type -band 2048) -ne 0) ('Expected deferred SYSTEM action: '+$name)
}
$registry = @(ReadMsiRows -Table 'Registry' -Columns @('Root','Key','Name','Value','Component_'))
Require (@($registry | Where-Object { $_.Key -eq 'Software\Microsoft\Windows\CurrentVersion\Run' -and $_.Name -eq 'ReyAudioControl' -and $_.Value -eq '"[#ControlExe]" --tray' }).Count -eq 1) 'Tray autorun is missing.'
$dark = Join-Path ([IO.Path]::GetFullPath($Wix)) 'dark.exe'
$arguments = @('-nologo','-x',$destination,$msi,'-o',(Join-Path $destination 'ReyAudio.wxs'))
& $dark @arguments *> (Join-Path $destination 'dark.log')
Require ($LASTEXITCODE -eq 0) 'CAB extraction failed; inspect dark.log.'
$sources = [ordered]@{
    ServiceExe = Join-Path $Bin 'ReyAudioService.exe'
    ControlExe = Join-Path $Bin 'ReyAudioControl.exe'
    ProbeExe = Join-Path $Bin 'ReyAudioProbe.exe'
    SetupHelper = Join-Path $setupRoot 'ReyAudioSetup.Helper.exe'
    DriverSysFile = Join-Path $Driver 'ReyAudioAcx.sys'
    DriverInfFile = Join-Path $Driver 'ReyAudioAcx.inf'
    DriverCatFile = Join-Path $Driver 'reyaudioacx.cat'
}
$payload = [ordered]@{}
foreach($pair in $sources.GetEnumerator()) {
    $extracted = Join-Path $destination ('File/'+$pair.Key)
    $hash = Digest $extracted
    Require ($hash -eq (Digest $pair.Value)) ('CAB hash mismatch: '+$pair.Key)
    $payload[$pair.Key] = $hash
}
$assembly = [Reflection.Assembly]::LoadFile($exe)
$resource = $assembly.GetManifestResourceStream('ReyAudio.Installer.msi')
Require ($null -ne $resource) 'EXE must contain the MSI.'
$sha = [Security.Cryptography.SHA256]::Create()
try { $embedded = [BitConverter]::ToString($sha.ComputeHash($resource)).Replace('-','').ToLowerInvariant() }
finally { $resource.Dispose(); $sha.Dispose() }
$msiHash = Digest $msi
Require ($embedded -eq $msiHash) 'EXE embeds another MSI.'
$report = [ordered]@{
    passed = $true
    version = '2.7.0-preview.1'
    inspected_utc = [DateTime]::UtcNow.ToString('o')
    msi_sha256 = $msiHash
    exe_sha256 = Digest $exe
    embedded_msi_verified = $true
    cab_payload_sha256 = $payload
    payload_count = $files.Count
    ini_or_install_scripts = $false
    service = $service
    firewall = $firewall
    driver_actions = $actions
    readiness_precedes_install = $true
    upgrade_in_transaction = $true
    rollback_commit_present = $true
    tray_autorun_verified = $true
    launch_conditions_verified = $true
    read_only_msi_environment = @{VersionNT64=$actualNt;NetFramework48=$actualFramework}
    machine_state_changed = $false
    kernel_loaded = $false
    elevated_install_qualified = $false
}
$report | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $destination 'inspection.json') -Encoding UTF8
Write-Output ('PASS: 7 payloads, service/firewall/actions, embedded MSI. '+(Join-Path $destination 'inspection.json'))
