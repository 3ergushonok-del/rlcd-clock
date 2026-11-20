# Wait for the port to appear (the board sleeps in deep sleep and has no port),
# then flash immediately. Run this, then press KEY/BOOT on the board.
# ASCII-only on purpose: Windows PowerShell 5.1 reads .ps1 as cp1251 and
# Cyrillic strings break parsing without a BOM.

param(
    [string]$Port = "COM11",
    [int]$WaitSeconds = 150
)

$ErrorActionPreference = "Stop"
$env:PYTHONUTF8 = "1"
$env:PYTHONIOENCODING = "utf-8"

$deadline = (Get-Date).AddSeconds($WaitSeconds)
$found = $false
while ((Get-Date) -lt $deadline) {
    if ([System.IO.Ports.SerialPort]::GetPortNames() -contains $Port) { $found = $true; break }
    Start-Sleep -Milliseconds 500
}

if (-not $found) {
    Write-Host "Port $Port did not appear in $WaitSeconds s - board did not wake." -ForegroundColor Yellow
    exit 1
}

Write-Host "Port $Port is up, flashing..." -ForegroundColor Cyan
python -m platformio run -t upload --upload-port $Port

if ($LASTEXITCODE -eq 0) {
    # Плата после прошивки проходит инициализацию и сеть, потом рисует экран.
    Start-Sleep -Seconds 10
    python tools/shot.py $Port preview.png
}