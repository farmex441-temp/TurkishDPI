<#
  Takes --ui-preview screenshots of TurkishDPI without administrator rights.

    powershell -ExecutionPolicy Bypass -File tools\ui_shots.ps1 -Out docs\design\screenshots\after

  Every scenario is captured at 100 %, plus -FullScaleScenarios at every
  scale in -Scales. Output: <scenario>[-<page>]@<percent>.png
#>
param(
    [string]$Exe = "build\x86_64\TurkishDPI.exe",
    [string]$Out = "docs\design\screenshots\after",
    [double[]]$Scales = @(1, 1.25, 1.5, 2),
    [string[]]$Scenarios = @("active", "active-long", "partial", "scanning", "first-run", "standby",
                             "paused", "offline", "error-driver", "error-av", "error-noprofile",
                             "banner-update", "banner-proxy", "banner-rival", "banner-multi",
                             "manual", "payment-off"),
    [string[]]$Pages = @("status"),
    [string[]]$FullScaleScenarios = @("active", "active-long")
)
$ErrorActionPreference = "Stop"
# "powershell -File" passes "a,b" as one string: split lists ourselves
$Scenarios = @($Scenarios | ForEach-Object { $_ -split "," } | Where-Object { $_ })
$Pages = @($Pages | ForEach-Object { $_ -split "," } | Where-Object { $_ })
$FullScaleScenarios = @($FullScaleScenarios | ForEach-Object { $_ -split "," } | Where-Object { $_ })
$Scales = @($Scales | ForEach-Object { "$_" -split "," } | Where-Object { $_ } |
            ForEach-Object { [double]::Parse($_, [Globalization.CultureInfo]::InvariantCulture) })
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root
New-Item -ItemType Directory -Force $Out | Out-Null
$tmp = Join-Path ([IO.Path]::GetTempPath()) ("tdpi_shots_" + [guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Force $tmp | Out-Null
$env:__COMPAT_LAYER = "RunAsInvoker"   # preview needs no elevation
$failed = 0
try {
    foreach ($s in $Scenarios) {
        foreach ($p in $Pages) {
            $sc = if ($FullScaleScenarios -contains $s) { $Scales } else { @(1) }
            foreach ($k in $sc) {
                $pct = [int][math]::Round($k * 100)
                $name = if ($p -eq "status") { "$s@$pct" } else { "$s-$p@$pct" }
                $bmp = Join-Path $tmp "$name.bmp"
                $kArg = $k.ToString([Globalization.CultureInfo]::InvariantCulture)
                $proc = Start-Process -FilePath $Exe -Wait -PassThru -NoNewWindow `
                    -ArgumentList @("--ui-preview", $s, "--scale", $kArg, "--page", $p, "--shot", "`"$bmp`"")
                if ($proc.ExitCode -ne 0) { Write-Warning "$name exit $($proc.ExitCode)"; $failed++; continue }
                & python -I tools\bmp_to_png.py $bmp (Join-Path $Out "$name.png")
                if ($LASTEXITCODE -ne 0) { Write-Warning "$name convert failed"; $failed++ }
                else { Write-Host "ok  $name" }
            }
        }
    }
} finally {
    Remove-Item -Recurse -Force $tmp -ErrorAction SilentlyContinue
}
if ($failed) { Write-Error "$failed screenshot(s) failed" }
