# Arma la carpeta de un ZIP de PRUEBA de la GUI (no es el ZIP oficial, que es de
# G5b: no lleva LEEME.txt ni AVISOS-TERCEROS.txt). Corre en la CI con pwsh 7:
#
#   tools/package-gui-test-windows.ps1 -Bundle <runner\Release> -CppZip <zip> -Out <carpeta>
#
# Lleva el bundle de `flutter build windows`, los .exe y .dll del ZIP de C++
# (tools/package-windows.sh) y las DLL de Visual C++ que pidan los ejecutables,
# sacadas del Visual Studio del runner: en una PC con una instalación mínima
# pueden no estar, y Microsoft permite el despliegue local junto a la app.
# Falla si alguien pide una DLL que no está en la carpeta ni es de Windows.
param(
    [Parameter(Mandatory)][string]$Bundle,
    [Parameter(Mandatory)][string]$CppZip,
    [Parameter(Mandatory)][string]$Out
)
$ErrorActionPreference = 'Stop'

New-Item -ItemType Directory -Force $Out | Out-Null
Copy-Item (Join-Path $Bundle '*') $Out -Recurse

$cpp = Join-Path ([IO.Path]::GetTempPath()) ([guid]::NewGuid())
Expand-Archive $CppZip $cpp
Copy-Item (Join-Path $cpp '*.exe'), (Join-Path $cpp '*.dll') $Out
Remove-Item $cpp -Recurse

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
    -property installationPath
$build = "$vs\VC\Auxiliary\Build"
$tools = (Get-Content "$build\Microsoft.VCToolsVersion.default.txt").Trim()
$redist = (Get-Content "$build\Microsoft.VCRedistVersion.default.txt").Trim()
$dumpbin = "$vs\VC\Tools\MSVC\$tools\bin\Hostx64\x64\dumpbin.exe"
$crt = (Get-ChildItem "$vs\VC\Redist\MSVC\$redist\x64" -Directory -Filter 'Microsoft.VC*.CRT').FullName
Write-Output "DLL de Visual C++ desde: $crt"

function Get-Binaries { Get-ChildItem $Out -File | Where-Object Extension -In '.exe', '.dll' }

function Get-Dependencies([string]$File) {
    $lines = & $dumpbin /nologo /dependents $File
    if ($LASTEXITCODE -ne 0) { throw "dumpbin falló con $File" }
    $lines | ForEach-Object {
        if ($_ -match '^\s+(\S+\.dll)\s*$') { $Matches[1].ToLowerInvariant() }
    }
}

# Hasta que no falte ninguna: msvcp140.dll, a su vez, pide las vcruntime140*.
# El runner las tiene en System32; una PC de la escuela, quizá no.
$visualCpp = '^(msvcp\d|vcruntime|concrt|vccorlib|vcomp|ucrtbased)'
do {
    $added = $false
    foreach ($binary in Get-Binaries) {
        foreach ($dep in Get-Dependencies $binary.FullName) {
            if ($dep -match $visualCpp -and -not (Test-Path (Join-Path $Out $dep))) {
                Copy-Item (Join-Path $crt $dep) $Out
                $added = $true
            }
        }
    }
} while ($added)

# El arranque en el runner no lo notaría: las encuentra en System32.
$missing = @('msvcp140.dll', 'vcruntime140.dll', 'vcruntime140_1.dll' |
    Where-Object { -not (Test-Path (Join-Path $Out $_)) } |
    ForEach-Object { "la carpeta no trae $_" })
foreach ($binary in Get-Binaries | Sort-Object Name) {
    $deps = @(Get-Dependencies $binary.FullName | Sort-Object)
    $local = @($deps | Where-Object { Test-Path (Join-Path $Out $_) })
    $system = @($deps | Where-Object { $_ -notin $local })
    Write-Output "$($binary.Name)"
    Write-Output "  de la carpeta: $($local -join ' ')"
    Write-Output "  de Windows:    $($system -join ' ')"
    foreach ($dep in $system) {
        if ($dep -notmatch '^(api|ext)-ms-win-' -and
            -not (Test-Path (Join-Path "$env:windir\System32" $dep))) {
            $missing += "$($binary.Name) pide $dep"
        }
    }
}
if ($missing) { throw "Faltan DLL:`n$($missing -join "`n")" }

$bytes = (Get-ChildItem $Out -File -Recurse | Measure-Object Length -Sum).Sum
Write-Output ("{0}: {1:N1} MB sin comprimir" -f $Out, ($bytes / 1MB))
