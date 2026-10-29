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

# Official Sentinel OS entry point. minios.ps1 remains the compatibility
# engine for the literal name in the professor rubric.
& (Join-Path $PSScriptRoot "minios.ps1") @PSBoundParameters
exit $LASTEXITCODE
