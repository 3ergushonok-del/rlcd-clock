# Wait for the board to answer over HTTP (it sleeps, so it must be woken by a
# button press), then flash it over Wi-Fi via OTA.
# ASCII-only on purpose: Windows PowerShell 5.1 reads .ps1 as cp1251.

param(
    [string]$Device = "rlcd-clock.local",
    [int]$WaitSeconds = 300
)

$ErrorActionPreference = "Stop"
$env:PYTHONUTF8 = "1"
$env:PYTHONIOENCODING = "utf-8"

$deadline = (Get-Date).AddSeconds($WaitSeconds)
$up = $false
while ((Get-Date) -lt $deadline) {
    try {
        $r = Invoke-WebRequest -Uri "http://$Device/status" -TimeoutSec 3 -UseBasicParsing
        if ($r.StatusCode -eq 200) { $up = $true; break }
    } catch { }
    Start-Sleep -Milliseconds 700
}

if (-not $up) {
    Write-Host "Device $Device did not answer in $WaitSeconds s - did it wake?" -ForegroundColor Yellow
    exit 1
}

Write-Host "Device is up, starting OTA upload..." -ForegroundColor Cyan

$env:PLATFORMIO_UPLOAD_PROTOCOL = "espota"
$env:PLATFORMIO_UPLOAD_PORT = $Device
python -m platformio run -t upload
if ($LASTEXITCODE -ne 0) {
    Write-Host "Env-var override failed, retrying with -O ..." -ForegroundColor Yellow
    python -m platformio run -t upload -O "upload_protocol=espota" -O "upload_port=$Device"
}

if ($LASTEXITCODE -eq 0) {
    Write-Host "OTA done." -ForegroundColor Green
} else {
    Write-Host "OTA failed." -ForegroundColor Red
}
