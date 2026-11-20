# Waits for the board: flashes over USB if the COM port shows up, otherwise
# over Wi-Fi (OTA) as soon as /status answers. Then triggers the partial-refresh
# self-test if the board is reachable.
# ASCII-only on purpose: Windows PowerShell 5.1 reads .ps1 as cp1251.

param(
    [string]$Port = "COM11",
    [string]$Device = "rlcd-clock.local",
    [int]$WaitSeconds = 3600
)

$ErrorActionPreference = "Stop"
$env:PYTHONUTF8 = "1"
$env:PYTHONIOENCODING = "utf-8"

function HttpUp([string]$ip) {
    try {
        $r = Invoke-WebRequest -Uri "http://$ip/status" -TimeoutSec 3 -UseBasicParsing
        return ($r.StatusCode -eq 200)
    } catch { return $false }
}

$deadline = (Get-Date).AddSeconds($WaitSeconds)
$viaUsb = $null

while ((Get-Date) -lt $deadline) {
    if ([System.IO.Ports.SerialPort]::GetPortNames() -contains $Port) { $viaUsb = $true; break }
    if (HttpUp $Device) { $viaUsb = $false; break }
    Start-Sleep -Milliseconds 700
}

if ($null -eq $viaUsb) {
    Write-Host "Board never showed up ($WaitSeconds s)." -ForegroundColor Yellow
    exit 1
}

if ($viaUsb) {
    Write-Host "COM port found, flashing over USB..." -ForegroundColor Cyan
    python -m platformio run -t upload --upload-port $Port
} else {
    Write-Host "Board answers over Wi-Fi, flashing over OTA..." -ForegroundColor Cyan
    $env:PLATFORMIO_UPLOAD_PROTOCOL = "espota"
    $env:PLATFORMIO_UPLOAD_PORT = $Device
    python -m platformio run -t upload
}

if ($LASTEXITCODE -ne 0) { Write-Host "Flash failed." -ForegroundColor Red; exit 1 }

Start-Sleep -Seconds 12

# Self-test of partial refresh: redraw every zone as separate strips.
$up = $false
for ($i = 0; $i -lt 20; $i++) {
    if (HttpUp $Device) { $up = $true; break }
    Start-Sleep -Seconds 2
}

if ($up) {
    try {
        Invoke-WebRequest -Uri "http://$Device/test?partial=1" -Method POST -TimeoutSec 6 -UseBasicParsing | Out-Null
        Write-Host "Partial-refresh self-test triggered: screen should look unchanged." -ForegroundColor Green
    } catch { Write-Host "Self-test request failed." -ForegroundColor Yellow }
} else {
    Write-Host "Board is not reachable after flashing (probably asleep)." -ForegroundColor Yellow
}
