param(
    [Parameter(Mandatory=$true)][string]$Bin,
    [string]$Output = 'build/usb-asio-setup'
)
$ErrorActionPreference = 'Stop'
$project = Split-Path $PSScriptRoot -Parent
$binary = [IO.Path]::GetFullPath($Bin)
$target = [IO.Path]::GetFullPath($Output)
[IO.Directory]::CreateDirectory($target) | Out-Null
$source = Join-Path $project 'installer/UsbAsioSetup'
$compiler = Join-Path $env:WINDIR 'Microsoft.NET/Framework64/v4.0.30319/csc.exe'
$payload = @('ReyAudioService.exe','ReyAudioAsio.dll','ReyAudioControl.exe','ReyAsioProbe.exe')
$hashCode = 'namespace ReyAudio.UsbAsioSetup { internal static class Payload { public static readonly System.Collections.Generic.Dictionary<string,string> Hashes = new System.Collections.Generic.Dictionary<string,string> {'
$manifest = [ordered]@{version='2.8.0-usb-preview'; purpose='One USB device, 8x8 USB-ASIO'; device_limit=1; transport='USB'; kernel_driver=$false; files=@{}}
foreach ($name in $payload) {
    $digest = (Get-FileHash -LiteralPath (Join-Path $binary $name) -Algorithm SHA256).Hash.ToLowerInvariant()
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
foreach ($name in $payload) { $arguments += '/resource:'+(Join-Path $binary $name)+',ReyAudio.Payload.'+$name }
$arguments += (Get-ChildItem -LiteralPath $source -Filter '*.cs').FullName
$arguments += $generated
& $compiler @arguments
if ($LASTEXITCODE) { throw 'USB-ASIO setup build failed.' }
$manifest.setup_sha256 = (Get-FileHash -LiteralPath $setup -Algorithm SHA256).Hash.ToLowerInvariant()
$manifest | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $target 'SHA256.json') -Encoding utf8
Write-Output ('Built '+$setup)
Get-FileHash -LiteralPath $setup -Algorithm SHA256 | Select-Object Path,Hash
