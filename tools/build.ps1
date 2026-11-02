param(
    [string]$VitaSdk = $env:VITASDK,
    [string]$CMake = 'cmake',
    [string]$Ninja = 'ninja',
    [string]$BuildDirectory
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$sourceDirectory = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if (-not $VitaSdk) { throw 'Indica -VitaSdk con la ruta absoluta al SDK.' }
$sdkDirectory = (Resolve-Path -LiteralPath $VitaSdk).Path
if (-not (Test-Path -LiteralPath (Join-Path $sdkDirectory 'bin\arm-vita-eabi-gcc.exe'))) {
    throw 'El SDK no contiene el compilador de Windows.'
}
if (-not $BuildDirectory) {
    $BuildDirectory = Join-Path ([IO.Path]::GetTempPath()) 'kirv-videoteca-build'
}
$buildPath = [IO.Path]::GetFullPath($BuildDirectory)
if ($buildPath.StartsWith($sourceDirectory + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Elige una carpeta de compilacion fuera del codigo de entrega.'
}
$cmakeExecutable = (Get-Command $CMake -ErrorAction Stop).Source
$ninjaExecutable = (Get-Command $Ninja -ErrorAction Stop).Source
$previousSdk = $env:VITASDK
$previousPath = $env:PATH
try {
    $env:VITASDK = $sdkDirectory.Replace('\','/')
    $env:PATH = "$(Join-Path $sdkDirectory 'bin');$env:PATH"
    & $cmakeExecutable -S $sourceDirectory -B $buildPath -G Ninja -DCMAKE_BUILD_TYPE=Release "-DCMAKE_MAKE_PROGRAM=$ninjaExecutable"
    if ($LASTEXITCODE) { throw 'Fallo la configuracion.' }
    & $cmakeExecutable --build $buildPath
    if ($LASTEXITCODE) { throw 'Fallo la compilacion.' }
    Write-Output "VPK generado: $(Join-Path $buildPath 'VitaMediaPlayerHardened-DEV.vpk')"
    Write-Output 'Ejecuta tools/verify_vpk.py sobre el VPK antes de instalarlo.'
} finally {
    $env:VITASDK = $previousSdk
    $env:PATH = $previousPath
}
