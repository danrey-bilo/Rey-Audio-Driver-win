param(
    [Parameter(Mandatory=$true)][string]$Bin,
    [Parameter(Mandatory=$true)][string]$Report,
    [int[]]$Rates=@(44100,48000,88200,96000,176400,192000)
)
$ErrorActionPreference='Stop'
$binPath=[IO.Path]::GetFullPath($Bin)
$reportPath=[IO.Path]::GetFullPath($Report)
[IO.Directory]::CreateDirectory($reportPath) | Out-Null
if(Get-Process -Name ReySarFixture -ErrorAction SilentlyContinue){throw 'A finite endpoint fixture is already running.'}
$service=Get-CimInstance Win32_Service -Filter "Name='ReyAudioService'"
if($service.State -eq 'Running' -and $service.PathName -notmatch '--asio-only'){throw 'Stop the endpoint-owning Rey service before a synthetic fixture test.'}
$results=@()
foreach($rate in $Rates) {
    if($rate -notin @(44100,48000,88200,96000,176400,192000)){throw 'Unsupported fixture rate.'}
    $log=Join-Path $reportPath ('fixture-'+$rate+'.txt')
    $fixture=Start-Process -FilePath (Join-Path $binPath 'ReySarFixture.exe') -ArgumentList @('12',$rate) -WindowStyle Hidden -RedirectStandardOutput $log -RedirectStandardError ($log+'.err') -PassThru
    $null=$fixture.Handle
    $jobs=@()
    try {
        $watch=[Diagnostics.Stopwatch]::StartNew()
        while($watch.ElapsedMilliseconds -lt 5000 -and -not ((Get-Content -LiteralPath $log -ErrorAction SilentlyContinue) -match 'SAR_FIXTURE_READY')) {Start-Sleep -Milliseconds 100}
        do {
            $lines=@(& (Join-Path $binPath 'ReyAudioProbe.exe'))
            if($lines -match 'selected=8$'){break}
            Start-Sleep -Milliseconds 200
        } while($watch.ElapsedMilliseconds -lt 8000)
        if(-not ($lines -match 'selected=8$')){throw ('Eight endpoints not active at '+$rate)}
        $lines | Set-Content -LiteralPath (Join-Path $reportPath ('endpoints-'+$rate+'.txt')) -Encoding utf8
        foreach($pair in '1-2','3-4','5-6','7-8') {
            $renderLine=@($lines | Where-Object {$_ -match ('^WASAPI_ENDPOINT flow=render name="Output '+$pair+' ' )})
            $captureLine=@($lines | Where-Object {$_ -match ('^WASAPI_ENDPOINT flow=capture name="Input '+$pair+' ' )})
            if($renderLine.Count -ne 1 -or $captureLine.Count -ne 1){throw ('Ambiguous pair '+$pair)}
            $render=[regex]::Match($renderLine[0],'id="([^"]+)"').Groups[1].Value
            $capture=[regex]::Match($captureLine[0],'id="([^"]+)"').Groups[1].Value
            $index=(([int]$pair.Split('-')[0])-1)/2
            $output=Join-Path $reportPath ('shared-'+$rate+'-'+$pair+'.txt')
            $process=Start-Process -FilePath (Join-Path $binPath 'ReySarSharedProbe.exe') -ArgumentList @($render,$capture,$index) -WindowStyle Hidden -RedirectStandardOutput $output -RedirectStandardError ($output+'.err') -PassThru
            $null=$process.Handle
            $jobs+=[pscustomobject]@{pair=$pair;process=$process;output=$output}
        }
        foreach($job in $jobs) {
            if(-not $job.process.WaitForExit(8000)){throw ('Timed out: '+$job.pair)}
            $result=[pscustomobject]@{rate=$rate;pair=$job.pair;exit_code=$job.process.ExitCode;output=[IO.File]::ReadAllLines($job.output)}
            $results+=$result; $result | ConvertTo-Json -Compress
        }
        if(-not $fixture.WaitForExit(15000)){throw 'Fixture did not finish.'}
        if($fixture.ExitCode){throw 'Endpoint fixture failed.'}
    } finally {
        foreach($job in $jobs){if(-not $job.process.HasExited){$job.process.Kill()}}
        if(-not $fixture.HasExited){$fixture.Kill();$fixture.WaitForExit(5000)|Out-Null}
    }
}
$results | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $reportPath 'results.json') -Encoding utf8
if(@($results | Where-Object {$_.exit_code -ne 0}).Count){throw 'Shared Windows PCM continuity failed; see results.json.'}
