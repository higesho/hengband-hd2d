param(
    [string]$Python = 'python',
    [string]$MSBuild = 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe',
    [string]$Upstream = $env:HENGBAND_UPSTREAM_ROOT,
    [string]$CoreSourceRoot = $env:HENGBAND_CORE_SOURCE_ROOT,
    [ValidateSet('Debug','Release')][string]$Configuration = 'Release',
    [switch]$Rebuild
)
$ErrorActionPreference = 'Stop'
$uiRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
if (-not $Upstream) { $Upstream = Join-Path (Split-Path $uiRoot -Parent) 'roguelike-cores/upstream' }
if (-not $CoreSourceRoot) { $CoreSourceRoot = Join-Path (Split-Path $uiRoot -Parent) 'roguelike-cores/build-sources' }
& $Python (Join-Path $PSScriptRoot 'prepare.py') --upstream $Upstream --output $CoreSourceRoot
if ($LASTEXITCODE -ne 0) { throw 'Core source preparation failed' }
foreach ($core in @('HengbandCore','TangbandCore','GensobandCore','SilCore','FroxCore')) {
    $buildTarget = if ($Rebuild) { 'Rebuild' } else { 'Build' }
    & $MSBuild (Join-Path $uiRoot "VisualStudio/$core/$core.vcxproj") /m /v:minimal /nologo "/t:$buildTarget" "/p:Configuration=$Configuration" /p:Platform=Win32 "/p:HengbandCoreSourceRoot=$CoreSourceRoot"
    if ($LASTEXITCODE -ne 0) { throw "Core build failed: $core" }
}
