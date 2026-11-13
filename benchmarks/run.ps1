# akt vs C, C++, Rust, Zig benchmark runner
#
#   powershell -ExecutionPolicy Bypass -File benchmarks/run.ps1
#
# Each benchmark runs 3 times; the best time is used.

param(
    [string]$Zig = "",
    [int]$Runs = 3
)

$ErrorActionPreference = "Continue"
[System.Threading.Thread]::CurrentThread.CurrentCulture = [System.Globalization.CultureInfo]::InvariantCulture
$root = Split-Path $PSScriptRoot -Parent
$suite = Join-Path $PSScriptRoot "suite"
$out = Join-Path $PSScriptRoot "out"
New-Item -ItemType Directory -Force -Path $out | Out-Null

$aktc = Join-Path $root "bin\aktc.exe"
if (-not (Test-Path $aktc)) { $aktc = "aktc" }

if (-not $Zig) {
    $cand = Get-Command zig -ErrorAction SilentlyContinue
    if ($cand) { $Zig = $cand.Source }
    else {
        $direct = "C:\Users\Vodonapor\AppData\Local\Temp\opencode\zig3\zig-windows-x86_64-0.13.0\zig.exe"
        if (Test-Path $direct) { $Zig = $direct }
        if (-not $Zig) {
            $found = Get-ChildItem "C:\Users\Vodonapor\AppData\Local\Temp\opencode" -Recurse -Filter "zig.exe" -ErrorAction SilentlyContinue | Select-Object -First 1
            if ($found) { $Zig = $found.FullName }
        }
    }
}

$bench = @("fib", "loop", "sieve", "matmul", "hash")
$langs = @("akt", "c", "cpp", "rust", "zig")

function Build-One($name, $lang) {
    $exe = Join-Path $out ($name + "_" + $lang + ".exe")
    if (Test-Path $exe) { Remove-Item $exe -Force -ErrorAction SilentlyContinue }
    switch ($lang) {
        "akt" {
            & $aktc build (Join-Path $suite ($name + ".akt")) -o $exe --release 2>$null | Out-Null
        }
        "c" {
            & clang -O3 -march=native (Join-Path $suite ($name + ".c")) -o $exe 2>$null | Out-Null
        }
        "cpp" {
            & clang++ -O3 -march=native (Join-Path $suite ($name + ".cpp")) -o $exe 2>$null | Out-Null
        }
        "rust" {
            & rustc -O (Join-Path $suite ($name + ".rs")) -o $exe 2>$null | Out-Null
        }
        "zig" {
            if ($Zig) {
                & $Zig build-exe (Join-Path $suite ($name + ".zig")) -O ReleaseFast ("-femit-bin=" + $exe) 2>$null | Out-Null
            }
        }
    }
    return $exe
}

function Time-Exe($exe) {
    $best = [double]::MaxValue
    $output = ""
    for ($i = 0; $i -lt $Runs; $i++) {
        $sw = [System.Diagnostics.Stopwatch]::StartNew()
        $output = (& $exe 2>$null | Out-String).Trim()
        $sw.Stop()
        $ms = $sw.Elapsed.TotalMilliseconds
        if ($ms -lt $best) { $best = $ms }
    }
    return @{ ms = $best; output = $output }
}

$results = @{}
$errors = @()

foreach ($name in $bench) {
    foreach ($lang in $langs) {
        $exe = Build-One $name $lang
        if (-not $exe -or -not (Test-Path $exe)) {
            $errors += ($name + "/" + $lang + ": build failed")
            continue
        }
        $r = Time-Exe $exe
        $key = $name + "/" + $lang
        $results[$key] = $r
        Write-Host ("{0,-8} {1,-5} {2,10:N1} ms   {3}" -f $name, $lang, $r.ms, $r.output)
    }
}

$md = @()
$md += "# Speed: akt vs C, C++, Rust, Zig"
$md += ""
$md += "Best of $Runs runs, Windows x64, clang -O3 -march=native, rustc -O, zig -O ReleaseFast."
$md += ""
$md += "| test | akt | C | C++ | Rust | Zig | akt/C |"
$md += "|---|---:|---:|---:|---:|---:|---:|"
foreach ($name in $bench) {
    $row = "| " + $name + " |"
    $base = $null
    $kbase = $name + "/c"
    if ($results.ContainsKey($kbase)) { $base = $results[$kbase].ms }
    foreach ($lang in @("akt", "c", "cpp", "rust", "zig")) {
        $k = $name + "/" + $lang
        if ($results.ContainsKey($k)) {
            $row += " " + $results[$k].ms.ToString("N1") + " |"
        } else {
            $row += " - |"
        }
    }
    $kakt = $name + "/akt"
    if ($base -and $results.ContainsKey($kakt)) {
        $ratio = $results[$kakt].ms / $base
        $row += " " + $ratio.ToString("N2") + "x |"
    } else {
        $row += " - |"
    }
    $md += $row
}
if ($errors.Count -gt 0) {
    $md += ""
    $md += "Build failures:"
    foreach ($e in $errors) { $md += "- $e" }
}
$md | Set-Content (Join-Path $PSScriptRoot "RESULTS.md") -Encoding ascii
Write-Host ""
Write-Host "written benchmarks/RESULTS.md"
