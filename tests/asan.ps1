$ErrorActionPreference = "Continue"
$root = Split-Path $PSScriptRoot -Parent
$env:AKT_ROOT = "C:\Users\Vodonapor\akt"
$aktc = Join-Path $root "bin\aktc.exe"
$res = (& clang -print-resource-dir 2>$null)
if ($res) { $env:Path = "$res\lib\windows;" + $env:Path }
$env:AKT_CFLAGS = "-fsanitize=address,undefined -fno-omit-frame-pointer -fno-lto -g"
$tests = @("hello.akt", "strings.akt", "map.akt", "json.akt", "regex.akt", "closures.akt", "weak_upgrade.akt", "thread_capture.akt", "async_deferred.akt")
$failed = 0
foreach ($t in $tests) {
    $file = Join-Path $PSScriptRoot "lang\$t"
    $out = & $aktc run $file 2>&1 | Out-String
    if ($LASTEXITCODE -ne 0) {
        Write-Host "ASan FAIL $t" -ForegroundColor Red
        Write-Host $out
        $failed++
    } else {
        Write-Host "ASan ok   $t" -ForegroundColor Green
    }
}
Remove-Item Env:AKT_CFLAGS
if ($failed) { exit 1 }
