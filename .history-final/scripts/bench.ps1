# Loopback throughput benchmark for uftp
param(
    [int]$FileSizeMB = 16,
    [int]$BasePort = 9100
)

$ErrorActionPreference = "Continue"
$Root = Split-Path -Parent $PSScriptRoot
$BuildDir = Join-Path $Root "build"
$Exe = Join-Path $BuildDir "uftp.exe"
$Gcc = "C:\msys64\ucrt64\bin\gcc.exe"

function Stop-Uftp {
    Get-Process uftp -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
    Start-Sleep -Milliseconds 200
}

function Ensure-Built {
    if (Test-Path $Exe) { return }
    if (-not (Test-Path $Gcc)) {
        throw "gcc not found at $Gcc and $Exe is missing"
    }
    New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null
    & $Gcc -std=c11 -Wall -Wextra -I (Join-Path $Root "include") -o $Exe `
        (Join-Path $Root "src\common.c") `
        (Join-Path $Root "src\codec.c") `
        (Join-Path $Root "src\net.c") `
        (Join-Path $Root "src\window.c") `
        (Join-Path $Root "src\fileio.c") `
        (Join-Path $Root "src\stats.c") `
        (Join-Path $Root "src\opts.c") `
        (Join-Path $Root "src\ui.c") `
        (Join-Path $Root "src\sender.c") `
        (Join-Path $Root "src\receiver.c") `
        (Join-Path $Root "src\main.c") -lws2_32
}

function Ensure-TestFile {
    param([string]$Path, [int]$SizeBytes)
    if (Test-Path $Path) { return }
    $rng = New-Object Random 42
    $buf = New-Object byte[] 1048576
    $fs = [IO.File]::Open($Path, [IO.FileMode]::CreateNew)
    try {
        $remaining = $SizeBytes
        while ($remaining -gt 0) {
            $rng.NextBytes($buf)
            $write = [Math]::Min($remaining, $buf.Length)
            $fs.Write($buf, 0, $write)
            $remaining -= $write
        }
    } finally {
        $fs.Close()
    }
}

function Run-UftpBench {
    param(
        [string]$Name,
        [int]$Port,
        [string]$InFile,
        [string]$Output,
        [string[]]$ExtraArgs
    )

    Stop-Uftp
    if (Test-Path $Output) { Remove-Item $Output -Force -ErrorAction SilentlyContinue }

    $recvArgs = @("--no-ui", "recv", "$Port", $Output)
    $recvProc = Start-Process -FilePath $Exe -ArgumentList $recvArgs `
        -WorkingDirectory $Root -PassThru -WindowStyle Hidden
    Start-Sleep -Milliseconds 500

    $sendArgs = @("--no-ui")
    if ($ExtraArgs.Count -gt 0) {
        $sendArgs += $ExtraArgs
    }
    $sendArgs += @("send", "127.0.0.1", "$Port", $InFile)
    $sendOut = & $Exe @sendArgs 2>&1 | ForEach-Object { "$_" } | Out-String

    $recvProc | Wait-Process -Timeout 60 -ErrorAction SilentlyContinue
    if (-not $recvProc.HasExited) {
        $recvProc | Stop-Process -Force -ErrorAction SilentlyContinue
    }
    Stop-Uftp

    $mbps = $null
    $retx = $null
    $verify = $null
    if ($sendOut -match "throughput:\s+([0-9.]+)\s+Mbps") {
        $mbps = [double]$Matches[1]
    }
    if ($sendOut -match "retransmits:\s+(\d+)") {
        $retx = [int]$Matches[1]
    }
    if ($sendOut -match "verify:\s+(\w+)") {
        $verify = $Matches[1]
    }

    [PSCustomObject]@{
        Tool = "uftp"
        Config = $Name
        Mbps = $mbps
        Retransmits = $retx
        Verify = $verify
        Notes = ($ExtraArgs -join " ")
    }
}

function Run-ScpBench {
    param(
        [string]$InFile,
        [string]$Output
    )

    $scp = Get-Command scp -ErrorAction SilentlyContinue
    if (-not $scp) { return $null }

    Stop-Uftp
    if (Test-Path $Output) { Remove-Item $Output -Force -ErrorAction SilentlyContinue }

    $sw = [Diagnostics.Stopwatch]::StartNew()
    & scp -q $InFile "127.0.0.1:$Output" 2>$null
    if ($LASTEXITCODE -ne 0) { return $null }
    $sw.Stop()

    $sizeBytes = (Get-Item $InFile).Length
    $sec = $sw.Elapsed.TotalSeconds
    if ($sec -le 0) { $sec = 0.001 }
    $mbps = ($sizeBytes * 8.0) / ($sec * 1000000.0)

    [PSCustomObject]@{
        Tool = "scp"
        Config = "loopback"
        Mbps = [Math]::Round($mbps, 2)
        Retransmits = "-"
        Verify = "-"
        Notes = "OpenSSH scp to 127.0.0.1 (if configured)"
    }
}

Stop-Uftp
Ensure-Built

$sizeBytes = $FileSizeMB * 1024 * 1024
$inputFile = Join-Path $BuildDir "bench_input.bin"
Ensure-TestFile -Path $inputFile -SizeBytes $sizeBytes

$configs = @(
    @{ Name = "default (w=64 mss=1400)"; Args = @() },
    @{ Name = "window=16"; Args = @("-w", "16") },
    @{ Name = "mss=512"; Args = @("-m", "512") },
    @{ Name = "w=16 mss=512"; Args = @("-w", "16", "-m", "512") }
)

$results = @()
$port = $BasePort
foreach ($cfg in $configs) {
    $outFile = Join-Path $BuildDir ("bench_out_{0}.bin" -f $port)
    $results += Run-UftpBench -Name $cfg.Name -Port $port `
        -InFile $inputFile -Output $outFile -ExtraArgs $cfg.Args
    $port++
}

$scpOut = Join-Path $BuildDir "bench_out_scp.bin"
$scpResult = Run-ScpBench -InFile $inputFile -Output $scpOut
if ($scpResult) { $results += $scpResult }

Stop-Uftp

Write-Output ""
Write-Output "| Tool | Config | Throughput | Retransmits | Verify | Notes |"
Write-Output "|------|--------|------------|-------------|--------|-------|"
foreach ($r in $results) {
    $mbpsText = if ($null -ne $r.Mbps) { "{0:N2} Mbps" -f $r.Mbps } else { "n/a" }
    Write-Output ("| {0} | {1} | {2} | {3} | {4} | {5} |" -f `
        $r.Tool, $r.Config, $mbpsText, $r.Retransmits, $r.Verify, $r.Notes)
}
