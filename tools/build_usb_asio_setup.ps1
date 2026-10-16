param(
    [Parameter(Mandatory=$true)][string]$Bin,
    [string]$Output = 'build/usb-asio-setup',
    [ValidatePattern('^[0-9]+\.[0-9]+\.[0-9]+(?:-[0-9A-Za-z.-]+)?$')]
    [string]$Version = '2.8.2-akm-dev.7'
)
$ErrorActionPreference = 'Stop'
$project = Split-Path $PSScriptRoot -Parent
$binary = [IO.Path]::GetFullPath($Bin)
if (Test-Path -LiteralPath (Join-Path $binary 'QUALIFICATION_ONLY')) { throw 'An isolated qualification build cannot be installed or published as a driver.' }
$target = [IO.Path]::GetFullPath($Output)
[IO.Directory]::CreateDirectory($target) | Out-Null
$source = Join-Path $project 'installer/UsbAsioSetup'
$compiler = Join-Path $env:WINDIR 'Microsoft.NET/Framework64/v4.0.30319/csc.exe'
$payload = @('ReyAudioService.exe','ReyAudioAsio.dll','ReyAudioControl.exe','ReyAsioProbe.exe')
$sources = @{}
foreach ($name in $payload) { $sources[$name] = Join-Path $binary $name }
$vendor = Join-Path $project 'external/sar-0.13.1'
$provenance = Get-Content -Raw -LiteralPath (Join-Path $vendor 'provenance.json') | ConvertFrom-Json
foreach ($entry in $provenance.runtime) {
    $actual = (Get-FileHash -LiteralPath (Join-Path $vendor ('runtime/'+$entry.name)) -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actual -ne $entry.sha256) { throw ('Original signed driver was modified: '+$entry.name) }
}
if ((Get-FileHash -LiteralPath (Join-Path $vendor 'upstream-source.zip') -Algorithm SHA256).Hash.ToLowerInvariant() -ne $provenance.source_archive_sha256) {
    throw 'Upstream driver source archive was modified'
}
foreach ($name in @('SynchronousAudioRouter.sys','SynchronousAudioRouter.inf','SynchronousAudioRouter.cat')) {
    $payload += $name; $sources[$name] = Join-Path $vendor ('runtime/'+$name)
}
foreach ($name in @('COPYING.txt','COPYING-V2.txt','provenance.json','upstream-source.zip')) {
    $destination = 'SAR-'+$name
    $payload += $destination; $sources[$destination] = Join-Path $vendor $name
}
$hashCode = 'namespace ReyAudio.UsbAsioSetup { internal static class Payload { public const string Version = "'+$Version+'"; public static readonly System.Collections.Generic.Dictionary<string,string> Hashes = new System.Collections.Generic.Dictionary<string,string> {'
$manifest = [ordered]@{version=$Version; purpose='One USB device, 8x8 USB-ASIO and four stereo Windows pairs in each direction'; device_limit=1; transport='USB'; kernel_driver='Original signed Synchronous Audio Router 0.13.1'; files=@{}}
foreach ($name in $payload) {
    $digest = (Get-FileHash -LiteralPath $sources[$name] -Algorithm SHA256).Hash.ToLowerInvariant()
    $hashCode += '{"'+$name+'","'+$digest+'"},'
    $manifest.files[$name] = $digest
}
$hashCode += '}; } }'
$generated = Join-Path $target 'Payload.cs'
[IO.File]::WriteAllText($generated,$hashCode)
$setup = Join-Path $target 'Rey-Audio-USB-ASIO-Setup-x64.exe'
$arguments = @('/nologo','/target:winexe','/platform:x64','/optimize+','/warn:4',
    ('/out:'+$setup), ('/win32manifest:'+(Join-Path $source 'app.manifest')),
    ('/win32icon:'+(Join-Path $project 'apps/ReyAudioControl/ReyAudio.ico')),
    '/reference:System.dll','/reference:System.Core.dll','/reference:System.Windows.Forms.dll',
    '/reference:System.ServiceProcess.dll')
foreach ($name in $payload) { $arguments += '/resource:'+$sources[$name]+',ReyAudio.Payload.'+$name }
$arguments += (Get-ChildItem -LiteralPath $source -Filter '*.cs').FullName
$arguments += $generated
& $compiler @arguments
if ($LASTEXITCODE) { throw 'USB-ASIO setup build failed.' }
$manifest.setup_sha256 = (Get-FileHash -LiteralPath $setup -Algorithm SHA256).Hash.ToLowerInvariant()
$manifest | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $target 'SHA256.json') -Encoding utf8
Write-Output ('Built '+$setup)
Get-FileHash -LiteralPath $setup -Algorithm SHA256 | Select-Object Path,Hash
