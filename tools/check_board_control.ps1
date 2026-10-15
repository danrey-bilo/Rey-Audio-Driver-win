param([string]$Output = 'build/board-control-checks.exe')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$framework = Join-Path $env:WINDIR 'Microsoft.NET/Framework64/v4.0.30319'
$target = [IO.Path]::GetFullPath((Join-Path $projectRoot $Output))
[IO.Directory]::CreateDirectory((Split-Path $target -Parent)) | Out-Null
$arguments = @('/nologo','/target:exe','/platform:x64','/optimize+','/warn:4','/main:ReyAudio.BoardChecks',('/out:'+$target))
foreach ($assembly in @('System.dll','System.Core.dll','System.Xaml.dll','System.Windows.Forms.dll','System.Drawing.dll','System.Web.Extensions.dll')) { $arguments += '/reference:'+(Join-Path $framework $assembly) }
foreach ($assembly in @('PresentationFramework.dll','PresentationCore.dll','WindowsBase.dll')) { $arguments += '/reference:'+(Join-Path $framework ('WPF/'+$assembly)) }
$arguments += (Get-ChildItem -LiteralPath (Join-Path $projectRoot 'apps/ReyAudioControl') -Filter '*.cs').FullName
$arguments += Join-Path $projectRoot 'tests/board_control.cs'
& (Join-Path $framework 'csc.exe') @arguments
if ($LASTEXITCODE) { throw 'Board control test build failed' }
& $target
if ($LASTEXITCODE) { throw 'Board control contracts failed' }
