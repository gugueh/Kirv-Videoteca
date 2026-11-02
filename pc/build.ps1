# Copyright (c) 2025-2026 gugueh (Kirv Studio). All rights reserved.
param(
    # Folder with ffmpeg.exe, ffprobe.exe and the build's LICENSE (for example a Gyan "essentials" build).
    [Parameter(Mandatory = $true)][string]$Ffmpeg,
    [string]$Python = 'python',
    [string]$Version = '0.1'
)
# Native tools print progress on stderr; failures are caught through $LASTEXITCODE.
$ErrorActionPreference = 'Continue'
Set-StrictMode -Version Latest
$pc = $PSScriptRoot
$repo = Split-Path $pc -Parent
$work = Join-Path ([IO.Path]::GetTempPath()) 'kirv-pc-build'
$exe = Get-ChildItem -Path $Ffmpeg -Recurse -Filter ffmpeg.exe | Select-Object -First 1
$probe = Get-ChildItem -Path $Ffmpeg -Recurse -Filter ffprobe.exe | Select-Object -First 1
if (-not $exe -or -not $probe) { throw 'ffmpeg.exe y ffprobe.exe no estan en la carpeta indicada.' }
$license = Get-ChildItem -Path $Ffmpeg -Recurse -Filter LICENSE* | Select-Object -First 1

if (Test-Path $work) { Remove-Item -Recurse -Force $work }
New-Item -ItemType Directory $work | Out-Null
& $Python -m venv (Join-Path $work 'venv')
$py = Join-Path $work 'venv\Scripts\python.exe'
& $py -m pip install --quiet --disable-pip-version-check pywebview pyinstaller pillow
if ($LASTEXITCODE) { throw 'No se pudieron instalar las dependencias.' }

# Same art as the Vita app.
$img = Join-Path $pc 'kirv_pc\ui\img'
New-Item -ItemType Directory -Force $img | Out-Null
Copy-Item (Join-Path $repo 'assets\ui\*.png') $img -Force
$icon = Join-Path $work 'kirv.ico'
& $py -c "from PIL import Image; Image.open(r'$repo\assets\icon0.png').save(r'$icon', sizes=[(16,16),(32,32),(48,48),(64,64),(128,128)])"

Push-Location $pc
try {
    & $py -m PyInstaller --noconfirm --clean --windowed --name 'Kirv Videoteca PC' --icon $icon `
        --distpath (Join-Path $work 'dist') --workpath (Join-Path $work 'build') --specpath $work `
        --add-data "$pc\kirv_pc\ui;kirv_pc\ui" (Join-Path $pc 'kirv_videoteca_pc.py')
    if ($LASTEXITCODE) { throw 'Fallo PyInstaller.' }
} finally { Pop-Location }

$app = Join-Path $work 'dist\Kirv Videoteca PC'
$bundled = Join-Path $app 'ffmpeg'
New-Item -ItemType Directory $bundled | Out-Null
Copy-Item $exe.FullName, $probe.FullName $bundled
if ($license) { Copy-Item $license.FullName (Join-Path $bundled 'LICENSE.txt') }
@"
ffmpeg $(& $exe.FullName -hide_banner -version | Select-Object -First 1)
Distribuido sin cambios bajo la licencia GPL (ver LICENSE.txt).
Codigo fuente de ffmpeg: https://ffmpeg.org/download.html
Compilacion usada: https://www.gyan.dev/ffmpeg/builds/
"@ | Set-Content -Encoding utf8 (Join-Path $bundled 'README.txt')

$zip = Join-Path $repo "..\Kirv-Videoteca-PC-$Version-windows.zip"
if (Test-Path $zip) { Remove-Item $zip }
Compress-Archive -Path $app -DestinationPath $zip
Write-Output "Listo: $([IO.Path]::GetFullPath($zip))"
