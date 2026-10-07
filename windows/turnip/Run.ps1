param(
    [Parameter(Mandatory)][string]$Program,
    [string[]]$ProgramArguments = @(),
    [string]$DriverDirectory = $PSScriptRoot,
    [ValidateSet('auto','sysmem','gmem','safe')][string]$Mode = 'auto'
)

$ErrorActionPreference = 'Stop'
$manifest = Join-Path $DriverDirectory 'freedreno_icd.aarch64.json'
if (!(Test-Path -LiteralPath $manifest) -or !(Test-Path -LiteralPath (Join-Path $DriverDirectory 'vulkan_freedreno.dll'))) {
    throw 'Use the extracted driver package, or pass -DriverDirectory.'
}
$saved = @{}
foreach ($name in @('VK_DRIVER_FILES','VK_ICD_FILENAMES','VK_LOADER_LAYERS_DISABLE','TU_DEBUG','TU_GSL_PROFILE','TU_GSL_POLL_COMPLETION','TU_GSL_LINEAR_WSI','TU_GSL_THREADED_WSI','PATH')) {
    $saved[$name] = [Environment]::GetEnvironmentVariable($name,'Process')
}
$exitCode = 0
try {
    $env:VK_DRIVER_FILES = [IO.Path]::GetFullPath($manifest)
    $env:VK_ICD_FILENAMES = $env:VK_DRIVER_FILES
    $env:VK_LOADER_LAYERS_DISABLE = '~implicit~'
    $env:TU_DEBUG = switch ($Mode) {
        'sysmem' { 'sysmem' }
        'gmem' { 'gmem' }
        'safe' { 'sysmem,noubwc' }
        default { $null }
    }
    $env:TU_GSL_PROFILE = $env:TU_GSL_POLL_COMPLETION = $env:TU_GSL_LINEAR_WSI = $env:TU_GSL_THREADED_WSI = '0'
    $env:PATH = "${DriverDirectory};$env:PATH"
    & $Program @ProgramArguments
    $exitCode = $LASTEXITCODE
} finally {
    foreach ($name in $saved.Keys) {
        [Environment]::SetEnvironmentVariable($name,$saved[$name],'Process')
    }
}
exit $exitCode
