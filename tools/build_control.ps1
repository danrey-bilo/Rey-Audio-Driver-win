param([string]$Output = "build/control/ReyAudioControl.exe")
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$frameworkRoot = Join-Path $env:WINDIR 'Microsoft.NET/Framework64/v4.0.30319'
$compiler = Join-Path $frameworkRoot 'csc.exe'
if (!(Test-Path -LiteralPath $compiler)) { throw '.NET Framework 4.8 x64 developer/compiler components are required.' }
$sourceRoot = Join-Path $projectRoot 'apps/ReyAudioControl'
$target = if ([IO.Path]::IsPathRooted($Output)) { $Output } else { Join-Path $projectRoot $Output }
[IO.Directory]::CreateDirectory((Split-Path $target -Parent)) | Out-Null
$arguments = @('/nologo','/target:winexe','/platform:x64','/optimize+','/warn:4',('/out:'+$target),
    ('/win32manifest:'+(Join-Path $sourceRoot 'app.manifest')),
    ('/win32icon:'+(Join-Path $sourceRoot 'ReyAudio.ico')),
    ('/resource:'+(Join-Path $sourceRoot 'MainWindow.xaml')+',ReyAudio.MainWindow.xaml'),
    ('/resource:'+(Join-Path $sourceRoot 'ReyAudio.ico')+',ReyAudio.ico'))
foreach ($assembly in @('System.dll','System.Core.dll','System.Xaml.dll','System.Windows.Forms.dll','System.Drawing.dll','System.Web.Extensions.dll')) {
    $arguments += '/reference:'+(Join-Path $frameworkRoot $assembly)
}
foreach ($assembly in @('PresentationFramework.dll','PresentationCore.dll','WindowsBase.dll')) {
    $arguments += '/reference:'+(Join-Path $frameworkRoot ('WPF/'+$assembly))
}
$arguments += (Get-ChildItem -LiteralPath $sourceRoot -Filter '*.cs').FullName
& $compiler @arguments
if ($LASTEXITCODE -ne 0) { throw 'Rey Audio control build failed.' }
Write-Output ('Built '+$target)
