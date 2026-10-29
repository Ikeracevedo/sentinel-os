[CmdletBinding()]
param(
    [Parameter()]
    [string]$Port = "COM8",

    [Parameter()]
    [ValidateRange(1, 65535)]
    [int]$DashboardPort = 8080,

    [Parameter()]
    [ValidateRange(1200, 2000000)]
    [int]$Baud = 921600,

    [Parameter()]
    [switch]$NoReset
)

# Motor compatible with the ./minios name in the professor rubric.
# sentinel.ps1 is the recommended branded entry point and delegates here.
$ErrorActionPreference = "Stop"
$Root = $PSScriptRoot
$Bridge = Join-Path $Root "tools\bridge.py"
$Database = Join-Path $Root "build\sentinel.db"

if (-not (Test-Path -LiteralPath $Bridge)) {
    throw "No encuentro tools\bridge.py. Ejecuta el lanzador desde el repositorio Sentinel OS."
}

$Python = Get-Command py -ErrorAction SilentlyContinue
if ($null -eq $Python) {
    $Python = Get-Command python -ErrorAction SilentlyContinue
}
if ($null -eq $Python) {
    throw "Python was not found. Install Python with pyserial to use the bridge."
}

New-Item -ItemType Directory -Path (Join-Path $Root "build") -Force | Out-Null

Write-Host "[sentinel] Sentinel OS - starting the 3 components..."
Write-Host ("[sentinel] 1/3 kernel .......... ESP32 {0} at {1} baud (reset on open)" -f $Port, $Baud)
Write-Host "[sentinel] 2/3 bridge .......... interactive UART console"
Write-Host "[sentinel] 3/3 dashboard ....... http://127.0.0.1:$DashboardPort"
Write-Host "[sentinel] ready. Ctrl+C closes bridge, dashboard and serial port cleanly."

$Arguments = @(
    $Bridge,
    "--port", $Port,
    "--baud", $Baud,
    "--serve", $DashboardPort,
    "--shell",
    "--sink", "sqlite",
    "--db", $Database
)
if (-not $NoReset) {
    $Arguments += "--reset"
}

$PythonPath = $Python.Source
& $PythonPath @Arguments
exit $LASTEXITCODE
