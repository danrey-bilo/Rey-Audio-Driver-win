# Compile-only API check: no runnable installer, payload packaging or elevation.
$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
$target=Join-Path $projectRoot 'build/installer-compile'
[IO.Directory]::CreateDirectory($target) | Out-Null
$fixture=Join-Path $target 'CompilePayload.cs'
[IO.File]::WriteAllText($fixture,'namespace ReyAudio.UsbAsioSetup { internal static class Payload { public const string Version="compile-check"; public static readonly System.Collections.Generic.Dictionary<string,string> Hashes=new System.Collections.Generic.Dictionary<string,string>(); } }')
$framework=Join-Path $env:WINDIR 'Microsoft.NET/Framework64/v4.0.30319'
$arguments=@('/nologo','/target:library','/platform:x64','/warn:4',('/out:'+(Join-Path $target 'InstallerCompileCheck.dll')),
    '/reference:System.dll','/reference:System.Core.dll','/reference:System.Windows.Forms.dll','/reference:System.ServiceProcess.dll')
$arguments+=(Get-ChildItem -LiteralPath (Join-Path $projectRoot 'installer/UsbAsioSetup') -Filter '*.cs').FullName
$arguments+=$fixture
& (Join-Path $framework 'csc.exe') @arguments
if ($LASTEXITCODE) {throw 'Installer compile check failed'}
Write-Output 'Installer API compiled; no installation or hardware check performed'
