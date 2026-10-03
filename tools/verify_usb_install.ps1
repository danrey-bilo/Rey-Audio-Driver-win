param(
    [Parameter(Mandatory=$true)][string]$SetupDirectory,
    [Parameter(Mandatory=$true)][string]$Output,
    [string]$Bin = "$env:ProgramFiles/ReyAudio/USBASIO"
)
$ErrorActionPreference = 'Stop'
if (-not [Environment]::Is64BitProcess) { throw 'Use x64 PowerShell.' }
$manifest = Get-Content -LiteralPath (Join-Path $SetupDirectory 'SHA256.json') -Raw | ConvertFrom-Json
$checks = @()
foreach ($entry in $manifest.files.PSObject.Properties) {
    $file = Join-Path $Bin $entry.Name
    $digest = (Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash.ToLowerInvariant()
    $checks += [ordered]@{name=$entry.Name; sha256=$digest; expected=$entry.Value; matches=($digest -eq $entry.Value); version=(Get-Item -LiteralPath $file).VersionInfo.FileVersion}
}
$setupHash = (Get-FileHash -LiteralPath (Join-Path $SetupDirectory 'Rey-Audio-USB-ASIO-Setup-x64.exe') -Algorithm SHA256).Hash.ToLowerInvariant()
$service = Get-CimInstance Win32_Service -Filter "Name='ReyAudioService'"
if ($null -eq $service) { throw 'ReyAudioService is not installed.' }
$process = if ($service.ProcessId) { Get-CimInstance Win32_Process -Filter ('ProcessId=' + $service.ProcessId) }
$profile = (Get-ItemProperty -LiteralPath 'HKLM:\Software\ReyAudio').UsbProfile
$dll = (Get-Item -LiteralPath 'HKLM:\Software\Classes\CLSID\{91BA2C4A-56BC-4C49-A499-231979EC5F60}\InprocServer32').GetValue('')
$asio = Get-ItemProperty -LiteralPath 'HKLM:\Software\ASIO\Rey Audio USB ASIO'
$block = 64; $lead = 3
if (Test-Path -LiteralPath 'HKCU:\Software\ReyAudio\ASIO') {
    $preferences = Get-ItemProperty -LiteralPath 'HKCU:\Software\ReyAudio\ASIO'
    if ($null -ne $preferences.BufferSize) { $block = $preferences.BufferSize }
    if ($null -ne $preferences.RenderLeadBlocks) { $lead = $preferences.RenderLeadBlocks }
}
$result = [ordered]@{
    version=$manifest.version; captured_utc=[DateTime]::UtcNow.ToString('o')
    payload=$checks; setup_sha256=$setupHash; setup_hash_matches=($setupHash -eq $manifest.setup_sha256)
    service=[ordered]@{state=$service.State; start_mode=$service.StartMode; command=$service.PathName; process_path=$process.ExecutablePath; process_path_available=($null -ne $process.ExecutablePath); process_name=$process.Name; process_id=$service.ProcessId}
    registered_dll=$dll; registered_clsid=$asio.CLSID
    saved_profile_bytes=$profile.Length; saved_profile_format=[BitConverter]::ToUInt32($profile,4)
    saved_asio_preferences=[ordered]@{block=$block; lead_blocks=$lead}
    install_log_tail=@(Get-Content -LiteralPath (Join-Path $Bin 'setup.log') -Tail 5)
}
$expectedExe = [IO.Path]::GetFullPath((Join-Path $Bin 'ReyAudioService.exe'))
$expectedDll = [IO.Path]::GetFullPath((Join-Path $Bin 'ReyAudioAsio.dll'))
$expectedCommand = '"' + $expectedExe + '" --service --asio-only'
$result.okay = ($checks.Where({-not $_.matches}).Count -eq 0 -and $result.setup_hash_matches -and $service.State -eq 'Running' -and $service.StartMode -eq 'Auto' -and $service.PathName -eq $expectedCommand -and $process.Name -eq 'ReyAudioService.exe' -and ($null -eq $process.ExecutablePath -or $process.ExecutablePath -eq $expectedExe) -and $dll -eq $expectedDll -and $asio.CLSID -eq '{91BA2C4A-56BC-4C49-A499-231979EC5F60}' -and $result.saved_profile_format -eq 3 -and $profile.Length -eq 168)
$destination = [IO.Path]::GetFullPath($Output)
[IO.Directory]::CreateDirectory((Split-Path -Parent $destination)) | Out-Null
$result | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $destination -Encoding utf8
[pscustomobject]$result | Select-Object version,okay,saved_profile_format,saved_profile_bytes | ConvertTo-Json
if (-not $result.okay) { throw 'USB-only installation verification failed.' }
