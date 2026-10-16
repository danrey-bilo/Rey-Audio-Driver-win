param([string]$Output = 'build/mixer-pair-checks.exe')
$ErrorActionPreference='Stop'
$reyProject=Split-Path $PSScriptRoot -Parent
$reyFramework=Join-Path $env:WINDIR 'Microsoft.NET/Framework64/v4.0.30319'
$reyTarget=[IO.Path]::GetFullPath((Join-Path $reyProject $Output))
[IO.Directory]::CreateDirectory((Split-Path $reyTarget -Parent)) | Out-Null
$reyArguments=@('/nologo','/target:exe','/platform:x64','/optimize+','/warn:4','/main:ReyAudio.MixerPairChecks',('/out:'+$reyTarget))
foreach($reyAssembly in @('System.dll','System.Core.dll','System.Xaml.dll','System.Windows.Forms.dll','System.Drawing.dll','System.Web.Extensions.dll')) { $reyArguments += '/reference:'+(Join-Path $reyFramework $reyAssembly) }
foreach($reyAssembly in @('PresentationFramework.dll','PresentationCore.dll','WindowsBase.dll')) { $reyArguments += '/reference:'+(Join-Path $reyFramework ('WPF/'+$reyAssembly)) }
$reyArguments+=(Get-ChildItem -LiteralPath (Join-Path $reyProject 'apps/ReyAudioControl') -Filter '*.cs').FullName
$reyArguments+=Join-Path $reyProject 'tests/mixer_pair.cs'
& (Join-Path $reyFramework 'csc.exe') @reyArguments
if($LASTEXITCODE){throw 'Mixer pair checks did not compile'}
& $reyTarget
if($LASTEXITCODE){throw 'Mixer pair checks failed'}
