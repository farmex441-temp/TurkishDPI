# Elevated end-to-end check of the real window: what a screen reader gets
# (native UI Automation via build\uia_probe.exe, see tests/uia_probe.c), the
# "start with Windows" toggle and its logon task, the tray icon and exit.
# Usage (admin PowerShell):
#   tools\uia_e2e.ps1 -Exe build\x86_64\TurkishDPI.exe -Probe build\uia_probe.exe -Out out.txt
# Leaves autostart OFF and the app closed.
param([string]$Exe, [string]$Probe, [string]$Out)
$ErrorActionPreference = 'Continue'
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
function W($s) { Add-Content -Path $Out -Value $s -Encoding UTF8 }
function P { $r = & $Probe @args 2>&1; $r | ForEach-Object { W "   $_" }; return $LASTEXITCODE }
Set-Content -Path $Out -Value "== uia e2e $(Get-Date -Format s) $Exe" -Encoding UTF8

function Task-State {
    $t = Get-ScheduledTask -TaskName TurkishDPI -ErrorAction SilentlyContinue
    if (-not $t) { return "no task" }
    return ("task: " + $t.Actions.Execute + " " + $t.Actions.Arguments + " user=" + $t.Principal.UserId +
            " run=" + $t.Principal.RunLevel + " logon=" + $t.Principal.LogonType +
            " battery-start-disallowed=" + $t.Settings.DisallowStartIfOnBatteries +
            " stop-on-battery=" + $t.Settings.StopIfGoingOnBatteries + " limit=" + $t.Settings.ExecutionTimeLimit +
            " trigger-user=" + ($t.Triggers | ForEach-Object { $_.UserId }))
}

W "== stop running instances"
Get-Process TurkishDPI -ErrorAction SilentlyContinue | ForEach-Object { W "stopping $($_.Id) $($_.Path)"; Stop-Process -Id $_.Id -Force }
Start-Sleep -Seconds 2
& "$env:SystemRoot\System32\schtasks.exe" /Delete /F /TN TurkishDPI 2>&1 | Out-Null
W ("before: " + (Task-State))

W "== self-test"
$st = Start-Process -FilePath $Exe -ArgumentList '--selftest' -PassThru -Wait
W "selftest exit code: $($st.ExitCode)"
Get-Content 'C:\ProgramData\TurkishDPI\TurkishDPI.log' -ErrorAction SilentlyContinue | Select-String 'SELFTEST' | Select-Object -Last 12 | ForEach-Object { W $_.Line }

W "== start window"
$proc = Start-Process -FilePath $Exe -PassThru
for ($i = 0; $i -lt 40; $i++) { Start-Sleep -Milliseconds 500; & $Probe focus *> $null; if ($LASTEXITCODE -ne 3) { break } }
Start-Sleep -Seconds 4
W "-- status page"; P dump | Out-Null
W "-- focus"; P focus | Out-Null
W "-- tray"; P tray | Out-Null

W "-- settings page"
P invoke "Ayarlar" | Out-Null
Start-Sleep -Seconds 1
P dump | Out-Null

W "== autostart ON"
P invoke "Windows a" | Out-Null
for ($i = 0; $i -lt 40; $i++) { Start-Sleep -Milliseconds 500; if (Get-ScheduledTask -TaskName TurkishDPI -ErrorAction SilentlyContinue) { break } }
Start-Sleep -Seconds 2
W ("after ON: " + (Task-State))
P dump | Select-String 'Windows' | Out-Null
W ("program files: " + ((Get-ChildItem 'C:\Program Files\TurkishDPI' -ErrorAction SilentlyContinue | ForEach-Object { $_.Name }) -join ', '))
W ("program files acl: " + ((icacls 'C:\Program Files\TurkishDPI' 2>&1) -join ' | '))
W ("xml left behind: " + (Test-Path 'C:\ProgramData\TurkishDPI\autostart.xml'))
$t = Get-ScheduledTask -TaskName TurkishDPI -ErrorAction SilentlyContinue
if ($t) {
    $exeHash = (Get-FileHash $Exe).Hash
    $pfHash = (Get-FileHash 'C:\Program Files\TurkishDPI\TurkishDPI.exe').Hash
    W ("program files exe = this build: " + ($exeHash -eq $pfHash))
}

W "== autostart OFF"
P invoke "Windows a" | Out-Null
for ($i = 0; $i -lt 40; $i++) { Start-Sleep -Milliseconds 500; if (-not (Get-ScheduledTask -TaskName TurkishDPI -ErrorAction SilentlyContinue)) { break } }
Start-Sleep -Seconds 2
W ("after OFF: " + (Task-State))

W "-- details page"
P invoke "Ayr" | Out-Null
Start-Sleep -Seconds 1
P dump | Out-Null
P invoke "Durum" | Out-Null

W "== log"
$log = Get-Content 'C:\ProgramData\TurkishDPI\TurkishDPI.log' -ErrorAction SilentlyContinue
$start = ($log | Select-String 'starting' | Select-Object -Last 1).LineNumber
if ($start) { $log[($start-1)..($log.Length-1)] | Where-Object { $_ -match '\[app\]' } | Select-Object -First 40 | ForEach-Object { W $_ } }

W "== exit through the window"
P invoke "Ayarlar" | Out-Null
Start-Sleep -Milliseconds 500
P invoke ([string][char]0x00C7 + [char]0x0131 + "k") | Out-Null   # "Çık(ış)", ASCII-only script
$gone = $proc.WaitForExit(20000)
W "exited cleanly: $gone"
if (-not $gone) { Get-Process TurkishDPI -ErrorAction SilentlyContinue | Stop-Process -Force }
Start-Sleep -Seconds 1
W ("TurkishDPI processes left: " + (Get-Process TurkishDPI -ErrorAction SilentlyContinue | Measure-Object).Count)
W "== done"
