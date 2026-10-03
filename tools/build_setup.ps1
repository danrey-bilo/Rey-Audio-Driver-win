param(
    [Parameter(Mandatory=$true)][string]$Bin,
    [Parameter(Mandatory=$true)][string]$Driver,
    [string]$Wix = '../.tools/wix3141',
    [string]$Output = 'build/setup',
    [switch]$HelperOnly
)
$ErrorActionPreference = 'Stop'
$project = Split-Path $PSScriptRoot -Parent
$target = [IO.Path]::GetFullPath($Output)
[IO.Directory]::CreateDirectory($target) | Out-Null
$source = Join-Path $project 'apps/ReyAudioSetup'
$framework = Join-Path $env:WINDIR 'Microsoft.NET/Framework64/v4.0.30319'
$compiler = Join-Path $framework 'csc.exe'
$payload = @('ReyAudioAcx.sys','ReyAudioAcx.inf','reyaudioacx.cat')
$hashCode = 'namespace ReyAudio.Setup { internal static class Payload { public static readonly System.Collections.Generic.Dictionary<string,string> Hashes = new System.Collections.Generic.Dictionary<string,string> {'
foreach ($name in $payload) {
    $digest = (Get-FileHash -LiteralPath (Join-Path $Driver $name) -Algorithm SHA256).Hash.ToLowerInvariant()
    $hashCode += '{"'+$name+'","'+$digest+'"},'
}
$hashCode += '}; } }'
$generated = Join-Path $target 'Payload.cs'
[IO.File]::WriteAllText($generated,$hashCode)
$arguments = @('/nologo','/target:winexe','/platform:x64','/optimize+','/warn:4',
    ('/win32manifest:'+(Join-Path $project 'apps/ReyAudioControl/app.manifest')),
    ('/win32icon:'+(Join-Path $project 'apps/ReyAudioControl/ReyAudio.ico')),
    '/reference:System.dll','/reference:System.Core.dll','/reference:System.Security.dll',
    '/reference:System.Drawing.dll','/reference:System.Windows.Forms.dll','/reference:System.Web.Extensions.dll')
$sources = (Get-ChildItem -LiteralPath $source -Filter '*.cs').FullName + @($generated)
$helper = Join-Path $target 'ReyAudioSetup.Helper.exe'
& $compiler @arguments ('/out:'+$helper) @sources
if ($LASTEXITCODE) { throw 'Setup helper build failed.' }
if ($HelperOnly) { Write-Output ('Built '+$helper); return }
$wixRoot = [IO.Path]::GetFullPath($Wix)
$msi = Join-Path $target 'Rey-Audio-Driver-2.7.0-preview.1-x64.msi'
$object = Join-Path $target 'ReyAudio.wixobj'
$licenseText = Get-Content -LiteralPath (Join-Path $project 'LICENSE') -Raw
$rtf = '{\rtf1\ansi\deff0{\fonttbl{\f0 Segoe UI;}}\f0\fs20 ' + ($licenseText.Replace('\','\\').Replace('{','\{').Replace('}','\}').Replace("`r`n",'\par ').Replace("`n",'\par ')) +
    '\par\par Rey Audio preview creates a local test certificate, trusts it on this PC, and installs its own audio driver. No paid certificate or shared private key is required. Windows Test Mode must already be active. Secure Boot and Core Isolation are not changed by MSI.}'
$license = Join-Path $target 'License.rtf'
[IO.File]::WriteAllText($license,$rtf)
$candleArguments = @('-nologo','-arch','x64','-ext','WixUtilExtension','-ext','WixFirewallExtension',
    ('-dBin='+[IO.Path]::GetFullPath($Bin)), ('-dDriver='+[IO.Path]::GetFullPath($Driver)),
    ('-dHelper='+$helper), ('-dLicense='+$license), ('-dIcon='+(Join-Path $project 'apps/ReyAudioControl/ReyAudio.ico')),
    '-out',$object,(Join-Path $project 'installer/ReyAudio.wxs'))
& (Join-Path $wixRoot 'candle.exe') @candleArguments
if ($LASTEXITCODE) { throw 'MSI compilation failed.' }
$lightArguments = @('-nologo','-ext','WixUIExtension','-ext','WixUtilExtension','-ext','WixFirewallExtension','-cultures:ru-ru','-loc',(Join-Path $project 'installer/Firewall.ru.wxl'),'-out',$msi,$object)
& (Join-Path $wixRoot 'light.exe') @lightArguments
if ($LASTEXITCODE) { throw 'MSI linking/validation failed.' }
$bootstrap = Join-Path $target 'Rey-Audio-Setup-2.7.0-preview.1-x64.exe'
& $compiler @arguments ('/out:'+$bootstrap) ('/resource:'+$msi+',ReyAudio.Installer.msi') @sources
if ($LASTEXITCODE) { throw 'Setup EXE build failed.' }
foreach($file in @($msi,$bootstrap)) { Get-FileHash -LiteralPath $file -Algorithm SHA256 | Select-Object Path,Hash }
