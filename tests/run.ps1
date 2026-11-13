$ErrorActionPreference = "Continue"
$root = Split-Path $PSScriptRoot -Parent
$asciiRoot = "C:\Users\Vodonapor\akt"
if (Test-Path $asciiRoot) { $env:AKT_ROOT = $asciiRoot } else { $env:AKT_ROOT = $root }
$aktc = Join-Path $root "bin\aktc.exe"
if (-not (Test-Path $aktc)) { throw "aktc.exe not built. Run: xmake build" }

Write-Host "== runtime tests =="
& (Join-Path $root "bin\akt_tests.exe")
if ($LASTEXITCODE -ne 0) { throw "runtime tests failed" }

$failed = 0
$total = 0
Write-Host "== language tests =="
Get-ChildItem (Join-Path $PSScriptRoot "lang\*.akt") | Sort-Object Name | ForEach-Object {
    $total++
    $file = $_.FullName
    $name = $_.BaseName
    $expectedFile = [System.IO.Path]::ChangeExtension($file, ".out")
    $actual = (& $aktc run $file 2>&1 | Out-String)
    $rc = $LASTEXITCODE
    $actual = (($actual.Replace("`r`n", "`n") -split "`n") |
        Where-Object { $_ -notmatch '^\s*LINK\s*:' -and $_ -notmatch 'LNK\d+' }) -join "`n"
    $actual = $actual.TrimEnd()
    $exitFile = [System.IO.Path]::ChangeExtension($file, ".exit")
    $expectedExit = 0
    if (Test-Path $exitFile) { $expectedExit = [int](Get-Content -Raw $exitFile).Trim() }
    if ($rc -ne $expectedExit) {
        Write-Host "FAIL $name (exit $rc, expected $expectedExit)" -ForegroundColor Red
        Write-Host $actual
        $failed++
        return
    }
    if (Test-Path $expectedFile) {
        $expected = (Get-Content -Raw $expectedFile).Replace("`r`n", "`n").TrimEnd()
        if ($actual -ne $expected) {
            Write-Host "FAIL $name (output mismatch)" -ForegroundColor Red
            Write-Host "--- expected ---"
            Write-Host $expected
            Write-Host "--- actual ---"
            Write-Host $actual
            $failed++
            return
        }
    }
    Write-Host "ok   $name" -ForegroundColor Green
}

Write-Host "== negative tests =="
Get-ChildItem (Join-Path $PSScriptRoot "negative\*.akt") | Sort-Object Name | ForEach-Object {
    $total++
    $file = $_.FullName
    $name = $_.BaseName
    $out = (& $aktc check $file 2>&1 | Out-String)
    $rc = $LASTEXITCODE
    if ($rc -eq 0) {
        Write-Host "FAIL $name (expected error)" -ForegroundColor Red
        $failed++
    } else {
        Write-Host "ok   $name (rejected)" -ForegroundColor Green
    }
}

Write-Host ""
if ($failed -ne 0) {
    Write-Host "$failed of $total tests failed" -ForegroundColor Red
    exit 1
}
Write-Host "all $total tests passed" -ForegroundColor Green
