# run_tests.ps1 - Kasturi test runner (Windows PowerShell 5.1 or PowerShell 7)
#
#   powershell -ExecutionPolicy Bypass -File tests\run_tests.ps1 [-Filter name] [-Update]
#
# tests\cases\NAME.kst     program; its output must equal NAME.out
#             NAME.in      optional standard input
#             NAME.args    optional program arguments, one per line
#             NAME.exit    optional expected exit code (default 0)
# tests\errors\NAME.kst    must be rejected; the first line is
#                          "// <label>: <text>" and the diagnostics must contain <text>
# -Update rewrites the .out files from the current output.
param([string]$Filter = '*', [switch]$Update)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$kasturi = Join-Path $root 'bin\kasturi.exe'
$work = Join-Path $root 'build\tests'
New-Item -ItemType Directory -Force $work | Out-Null
$utf8 = New-Object System.Text.UTF8Encoding $false

function Quote([string]$a) {
    if ($a -eq '') { return '""' }
    if ($a -notmatch '[\s"]') { return $a }
    return '"' + ($a -replace '(\\*)"', '$1$1\"' -replace '(\\+)$', '$1$1') + '"'
}

function Invoke-Program([string]$exe, [string[]]$argList, [string]$stdinFile) {
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $exe
    $psi.Arguments = (@($argList) | ForEach-Object { Quote $_ }) -join ' '
    $psi.UseShellExecute = $false
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $psi.RedirectStandardInput = $true
    $psi.StandardOutputEncoding = $utf8
    $psi.StandardErrorEncoding = $utf8
    $psi.WorkingDirectory = $work
    $p = [System.Diagnostics.Process]::Start($psi)
    $outTask = $p.StandardOutput.ReadToEndAsync()
    $errTask = $p.StandardError.ReadToEndAsync()
    if ($stdinFile) {
        $bytes = [System.IO.File]::ReadAllBytes($stdinFile)
        $p.StandardInput.BaseStream.Write($bytes, 0, $bytes.Length)
    }
    $p.StandardInput.Close()
    if (-not $p.WaitForExit(60000)) {
        $p.Kill()
        return @{ Code = -999; Out = ''; Err = 'timeout' }
    }
    $p.WaitForExit()
    return @{ Code = $p.ExitCode; Out = $outTask.Result; Err = $errTask.Result }
}

function Normalize([string]$s) { return ($s -replace "`r`n", "`n").TrimEnd("`n") }

$pass = 0; $fail = 0; $failed = @()

foreach ($src in Get-ChildItem (Join-Path $PSScriptRoot 'cases') -Filter "$Filter.kst" | Sort-Object Name) {
    $name = $src.BaseName
    $base = Join-Path $src.DirectoryName $name
    $exe = Join-Path $work "$name.exe"
    $c = Invoke-Program $kasturi @('-o', $exe, $src.FullName) $null
    if ($c.Code -ne 0) {
        Write-Host "FAIL  $name (compile)" -ForegroundColor Red
        Write-Host $c.Err
        $fail++; $failed += $name; continue
    }
    $argList = @()
    if (Test-Path "$base.args") { $argList = @([System.IO.File]::ReadAllLines("$base.args", $utf8) | Where-Object { $_ -ne '' }) }
    $stdin = if (Test-Path "$base.in") { "$base.in" } else { $null }
    $r = Invoke-Program $exe $argList $stdin
    $wantCode = 0
    if (Test-Path "$base.exit") { $wantCode = [int]([System.IO.File]::ReadAllText("$base.exit").Trim()) }
    $got = Normalize ($r.Out + $(if ($r.Err) { "`n[stderr]`n" + $r.Err } else { '' }))
    if ($Update) {
        [System.IO.File]::WriteAllText("$base.out", $got + "`n", $utf8)
        Write-Host "UPDATED $name"
        continue
    }
    $want = if (Test-Path "$base.out") { Normalize ([System.IO.File]::ReadAllText("$base.out", $utf8)) } else { '<missing .out>' }
    if ($got -ceq $want -and $r.Code -eq $wantCode) {
        Write-Host "ok    $name" -ForegroundColor Green
        $pass++
    } else {
        Write-Host "FAIL  $name (exit $($r.Code), expected $wantCode)" -ForegroundColor Red
        $gl = $got -split "`n"; $wl = $want -split "`n"
        for ($i = 0; $i -lt [Math]::Max($gl.Count, $wl.Count); $i++) {
            $g = if ($i -lt $gl.Count) { $gl[$i] } else { '<eof>' }
            $w = if ($i -lt $wl.Count) { $wl[$i] } else { '<eof>' }
            if ($g -cne $w) { Write-Host "  line $($i + 1):`n    got:  $g`n    want: $w"; break }
        }
        $fail++; $failed += $name
    }
}

foreach ($src in Get-ChildItem (Join-Path $PSScriptRoot 'errors') -Filter "$Filter.kst" | Sort-Object Name) {
    $name = 'errors/' + $src.BaseName
    $first = ([System.IO.File]::ReadAllLines($src.FullName, $utf8))[0]
    $expect = ($first -replace '^//\s*[^:]+:\s*', '').Trim()
    $c = Invoke-Program $kasturi @('-o', (Join-Path $work 'error_case.exe'), $src.FullName) $null
    if ($c.Code -ne 0 -and $c.Err.Contains($expect)) {
        Write-Host "ok    $name" -ForegroundColor Green
        $pass++
    } else {
        Write-Host "FAIL  $name (exit $($c.Code)); expected diagnostic containing: $expect" -ForegroundColor Red
        Write-Host $c.Err
        $fail++; $failed += $name
    }
}

Write-Host ""
Write-Host "$pass passed, $fail failed"
if ($fail -gt 0) { Write-Host ("failed: " + ($failed -join ', ')); exit 1 }
exit 0
