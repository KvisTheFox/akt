$ErrorActionPreference = "Continue"
$root = Split-Path $PSScriptRoot -Parent
$env:AKT_ROOT = "C:\Users\Vodonapor\akt"
$aktc = Join-Path $root "bin\aktc.exe"
$tmp = Join-Path $env:TEMP "akt-fuzz"
New-Item -ItemType Directory -Force -Path $tmp | Out-Null

$tokens = @(
    "fn", "main", "int", "string", "struct", "enum", "switch", "case", "return",
    "if", "else", "for", "in", "while", "loop", "defer", "arena", "use",
    "Option", "Result", "Some", "None", "Ok", "Err", "Map", "Set", "Vector",
    "{", "}", "(", ")", "[", "]", ";", ",", ".", ":", "::", "=", "==", "+",
    "-", "*", "/", "%", "?", "??", "as", "0", "1", "42", "3.14", '"s"', "'a'"
)

Write-Host "fuzzing parser with random token streams..."
$fail = 0
for ($i = 0; $i -lt 120; $i++) {
    $n = Get-Random -Minimum 1 -Maximum 40
    $parts = for ($j = 0; $j -lt $n; $j++) { $tokens | Get-Random }
    $src = ($parts -join " ")
    $file = Join-Path $tmp ("fuzz_{0}.akt" -f $i)
    Set-Content -Path $file -Value $src -Encoding ascii
    $out = Join-Path $tmp ("fuzz_{0}.out" -f $i)
    $err = Join-Path $tmp ("fuzz_{0}.err" -f $i)
    $p = Start-Process -FilePath $aktc -ArgumentList @("check", $file) -Wait -PassThru -NoNewWindow -RedirectStandardOutput $out -RedirectStandardError $err
    if ($p.ExitCode -ne 0 -and $p.ExitCode -ne 1 -and $p.ExitCode -ne 2) {
        Write-Host "CRASH/UNEXPECTED EXIT $($p.ExitCode) on $file" -ForegroundColor Red
        Get-Content $err
        $fail++
    }
    Remove-Item $file, $out, $err -Force -ErrorAction SilentlyContinue
}
if ($fail -ne 0) { Write-Host "$fail fuzz failures" -ForegroundColor Red; exit 1 }
Write-Host "fuzz ok" -ForegroundColor Green
