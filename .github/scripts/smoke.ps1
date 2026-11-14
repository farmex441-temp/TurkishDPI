<#
  Smoke test of the release zips exactly as shipped (run by the "smoke" job on
  windows-latest, never elevated-driver work):

    smoke.ps1 -Dist <folder with TurkishDPI-<tag>-x64.zip / -x86.zip> -Work <scratch> -Shots <out>

  Per architecture:
    - the text files in the zip have CRLF line endings (Notepad on Windows
      7/8.1), BENIOKU.txt starts with a UTF-8 BOM
    - TurkishDPI.exe --engine --help prints the usage and exits 0
    - every --ui-preview scenario of tools/ui_shots.ps1 exits 0 and writes a
      non-empty BMP
  Not run on purpose: --help (message box), --selftest and --engine with real
  arguments (they would open the WinDivert driver on the elevated runner).
#>
param(
    [Parameter(Mandatory)][string]$Dist,
    [Parameter(Mandatory)][string]$Work,
    [Parameter(Mandatory)][string]$Shots
)
$ErrorActionPreference = 'Stop'
$env:__COMPAT_LAYER = 'RunAsInvoker'   # the manifest asks for admin; preview needs none
$failures = New-Object System.Collections.Generic.List[string]
function Fail([string]$msg) { Write-Host "::error::$msg"; $failures.Add($msg) }

# Runs a process with a time limit; a hang fails the job instead of blocking it.
function Invoke-Timed([string]$Exe, [string[]]$ArgList, [int]$Seconds, [string]$StdOutFile) {
    $sp = @{ FilePath = $Exe; ArgumentList = $ArgList; PassThru = $true; NoNewWindow = $true }
    if ($StdOutFile) {
        $sp.RedirectStandardOutput = $StdOutFile
        $sp.RedirectStandardError = "$StdOutFile.err"
    }
    $p = Start-Process @sp
    $null = $p.Handle                  # keeps ExitCode readable after WaitForExit
    if (-not $p.WaitForExit($Seconds * 1000)) {
        try { $p.Kill() } catch { }
        return -9999                   # timeout
    }
    return $p.ExitCode
}

# The scenario list of tools/ui_shots.ps1 (its -Scenarios default), so a new
# scenario is smoke-tested without touching this script.
$shotsScript = Join-Path $PSScriptRoot '..\..\tools\ui_shots.ps1'
$ast = [System.Management.Automation.Language.Parser]::ParseFile((Resolve-Path $shotsScript), [ref]$null, [ref]$null)
$param = $ast.ParamBlock.Parameters | Where-Object { $_.Name.VariablePath.UserPath -eq 'Scenarios' }
$scenarios = @($param.DefaultValue.FindAll({ param($n) $n -is [System.Management.Automation.Language.StringConstantExpressionAst] }, $true) |
               ForEach-Object { $_.Value })
if ($scenarios.Count -lt 10) { throw "could not read the scenario list from tools/ui_shots.ps1 (got $($scenarios.Count))" }
Write-Host "scenarios ($($scenarios.Count)): $($scenarios -join ', ')"

foreach ($w in 'x64', 'x86') {
    Write-Host "::group::$w"
    $zip = Get-ChildItem -Path $Dist -Filter "TurkishDPI-*-$w.zip" | Select-Object -First 1
    if (-not $zip) { Fail "${w}: zip not found in $Dist"; Write-Host '::endgroup::'; continue }
    $dir = Join-Path $Work $w
    Expand-Archive -Path $zip.FullName -DestinationPath $dir -Force
    $pkg = Get-ChildItem -Path $dir -Directory | Select-Object -First 1
    $exe = Join-Path $pkg.FullName 'TurkishDPI.exe'
    Write-Host "$($zip.Name) -> $($pkg.FullName)"
    Get-ChildItem $pkg.FullName | Format-Table Name, Length | Out-String | Write-Host

    foreach ($f in 'TurkishDPI.exe', 'WinDivert.dll', 'LICENSE', 'NOTICE', 'THIRD_PARTY_LICENSES.txt',
                   'WinDivert-LICENSE.txt', 'BENIOKU.txt') {
        if (-not (Test-Path (Join-Path $pkg.FullName $f))) { Fail "${w}: $f missing from the zip" }
    }
    if (-not (Test-Path $exe)) { Write-Host '::endgroup::'; continue }

    # text files: CRLF only (as many LFs as CRLFs), BENIOKU.txt with UTF-8 BOM
    foreach ($f in 'BENIOKU.txt', 'LICENSE', 'NOTICE', 'THIRD_PARTY_LICENSES.txt', 'WinDivert-LICENSE.txt') {
        $path = Join-Path $pkg.FullName $f
        if (-not (Test-Path $path)) { continue }
        $b = [IO.File]::ReadAllBytes($path)
        $lf = 0; $crlf = 0
        for ($i = 0; $i -lt $b.Length; $i++) {
            if ($b[$i] -eq 10) { $lf++; if ($i -gt 0 -and $b[$i - 1] -eq 13) { $crlf++ } }
        }
        if ($lf -ne $crlf) { Fail "${w}: $f has $($lf - $crlf) line(s) with a bare LF" }
        if ($f -eq 'BENIOKU.txt' -and -not ($b.Length -ge 3 -and $b[0] -eq 0xEF -and $b[1] -eq 0xBB -and $b[2] -eq 0xBF)) {
            Fail "${w}: BENIOKU.txt has no UTF-8 BOM"
        }
    }

    # engine usage
    $out = Join-Path $Work "$w-engine-help.txt"
    $code = Invoke-Timed $exe @('--engine', '--help') 30 $out
    $text = if (Test-Path $out) { Get-Content -Raw $out } else { '' }
    Write-Host "--engine --help: exit $code, $($text.Length) chars of output"
    if ($code -ne 0) { Fail "${w}: --engine --help exit code $code (expected 0; -9999 = timeout)" }
    elseif ($text -notmatch 'TurkishDPI engine') { Fail "${w}: --engine --help printed no usage text" }

    # every UI preview scenario (+ the other two pages for one of them)
    $shotDir = Join-Path $Shots $w
    New-Item -ItemType Directory -Force $shotDir | Out-Null
    $jobs = @()
    foreach ($s in $scenarios) { $jobs += , @($s, 'status') }
    $jobs += , @('active', 'details'), @('active', 'settings')
    foreach ($j in $jobs) {
        $s = $j[0]; $page = $j[1]
        $name = if ($page -eq 'status') { $s } else { "$s-$page" }
        $bmp = Join-Path $shotDir "$name.bmp"
        $code = Invoke-Timed $exe @('--ui-preview', $s, '--page', $page, '--shot', "`"$bmp`"") 60 $null
        if ($code -ne 0) { Fail "${w}: --ui-preview $s --page $page exit code $code (-9999 = timeout)"; continue }
        if (-not (Test-Path $bmp) -or (Get-Item $bmp).Length -lt 1000) { Fail "${w}: --ui-preview $s --page $page wrote no/empty BMP"; continue }
        $hdr = [IO.File]::ReadAllBytes($bmp)[0..1]
        if ($hdr[0] -ne 0x42 -or $hdr[1] -ne 0x4D) { Fail "${w}: $name.bmp is not a BMP" }
        else { Write-Host "ok  $w $name ($((Get-Item $bmp).Length) bytes)" }
    }
    Write-Host '::endgroup::'
}

if ($failures.Count) {
    Write-Host "::error::smoke test: $($failures.Count) failure(s)"
    exit 1
}
Write-Host 'smoke test passed'
