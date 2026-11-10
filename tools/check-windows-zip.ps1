# Prueba el ZIP oficial de Windows (tools/package-windows.sh) como en una PC
# sin MSYS2 (corre en la CI con pwsh 7, en un runner sin MSYS2):
#
#   tools/check-windows-zip.ps1 -Zip <moss-X.Y.Z-windows-x64.zip> -Version <X.Y.Z>
#
# Lo descomprime en una ruta con espacios y acentos y, con solo Windows en el
# PATH:
# 1. Los recursos de versión de moss-gui.exe, moss.exe y moss-rpc.exe:
#    ProductName Moss, Ricky182771 y ProductVersion y FileVersion = -Version.
# 2. moss.exe --version y moss-rpc.exe --version.
# 3. Sin moss.portable: moss-rpc.exe responde initialize (portable:false),
#    shutdown y exit; moss.exe sin key sale con 1 y el error de configuración.
#    Ninguno crea datos\.
# 4. Con moss.portable: moss.exe crea datos\; moss-rpc.exe dice portable:true;
#    moss-gui.exe abre su ventana (primer cuadro, con el mínimo de 720×520) y
#    arranca el moss-rpc.exe de su carpeta, que usa datos\; al cerrar la
#    ventana no queda ningún moss-rpc.exe vivo.
param(
    [Parameter(Mandatory)][string]$Zip,
    [Parameter(Mandatory)][string]$Version
)
$ErrorActionPreference = 'Stop'

function Wait-Until([scriptblock]$Condition, [int]$Seconds, [string]$What) {
    $limit = (Get-Date).AddSeconds($Seconds)
    while (-not (& $Condition)) {
        if ((Get-Date) -gt $limit) { throw "Pasaron $Seconds s sin que: $What" }
        Start-Sleep -Milliseconds 250
    }
}

$root = Join-Path ([IO.Path]::GetTempPath()) "Prueba José Pérez $([guid]::NewGuid())"
New-Item -ItemType Directory $root | Out-Null
Expand-Archive $Zip $root
$dir = Join-Path $root "moss-$Version"
if (-not (Test-Path (Join-Path $dir 'moss-gui.exe'))) { throw "El ZIP no trae moss-$Version\moss-gui.exe" }
$env:PATH = "$env:windir\System32;$env:windir"
foreach ($name in 'CHAT_API_KEY', 'CHAT_BASE_URL', 'CHAT_MODEL', 'CHAT_DATA_DIR', 'CHAT_DOWNLOAD_DIR') {
    Remove-Item "env:$name" -ErrorAction Ignore
}
$datos = Join-Path $dir 'datos'

# 1. Recursos de versión.
foreach ($exe in 'moss-gui.exe', 'moss.exe', 'moss-rpc.exe') {
    $info = (Get-Item (Join-Path $dir $exe)).VersionInfo
    Write-Output ("{0}: {1} {2} · {3} · {4}" -f $exe, $info.ProductName, $info.ProductVersion,
        $info.FileDescription, $info.CompanyName)
    if ($info.ProductName -ne 'Moss' -or $info.CompanyName -ne 'Ricky182771' -or
        $info.ProductVersion -ne $Version -or $info.FileVersion -ne $Version -or
        -not $info.FileDescription) {
        throw "$exe no trae los recursos de versión esperados (versión $Version)"
    }
}

# 2. --version.
foreach ($exe in 'moss', 'moss-rpc') {
    $said = & (Join-Path $dir "$exe.exe") --version
    if ($LASTEXITCODE -ne 0 -or $said -ne "$exe $Version") {
        throw "$exe.exe --version dijo '$said' (código $LASTEXITCODE; -1073741515 es una DLL que falta)"
    }
    Write-Output $said
}

# moss-rpc.exe con initialize, shutdown y exit; devuelve lo que respondió.
function Test-Rpc {
    $in = Join-Path $root 'rpc-in.txt'
    $out = Join-Path $root 'rpc-out.txt'
    $err = Join-Path $root 'rpc-err.txt'
    @(
        '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocol_version":1,"client":{"name":"ci","version":"0"}}}'
        '{"jsonrpc":"2.0","id":2,"method":"shutdown","params":{}}'
        '{"jsonrpc":"2.0","method":"exit"}'
    ) | Set-Content -Encoding utf8NoBOM $in
    $rpc = Start-Process -FilePath (Join-Path $dir 'moss-rpc.exe') -WorkingDirectory $dir -NoNewWindow `
        -PassThru -Wait -RedirectStandardInput $in -RedirectStandardOutput $out -RedirectStandardError $err
    $reply = Get-Content -Raw -Encoding utf8 $out
    if ($rpc.ExitCode -ne 0) {
        Get-Content $err
        throw "moss-rpc.exe salió con $($rpc.ExitCode)"
    }
    if ($reply -notmatch '"jsonrpc":"2.0","id":2,"result":\{\}') { throw "shutdown no respondió: $reply" }
    $reply
}

# moss.exe sin key ni consola: sale con 1 y el error de configuración.
function Test-Moss {
    $in = Join-Path $root 'moss-in.txt'
    $err = Join-Path $root 'moss-err.txt'
    Set-Content $in ''
    $moss = Start-Process -FilePath (Join-Path $dir 'moss.exe') -WorkingDirectory $dir -NoNewWindow `
        -PassThru -Wait -RedirectStandardInput $in -RedirectStandardOutput (Join-Path $root 'moss-out.txt') `
        -RedirectStandardError $err
    $text = Get-Content -Raw -Encoding utf8 $err
    Write-Output "moss.exe: código $($moss.ExitCode)"
    if ($moss.ExitCode -ne 1 -or $text -notmatch 'error de configuración' -or $text -notmatch 'CHAT_API_KEY') {
        Write-Output $text
        throw 'moss.exe sin key debía salir con 1 y el error de configuración'
    }
}

# 3. Sin moss.portable.
if ((Test-Rpc) -notmatch '"portable":false') { throw 'moss-rpc.exe sin moss.portable no dijo portable:false' }
Test-Moss
if (Test-Path $datos) { throw 'Sin moss.portable se creó datos\' }
Write-Output 'Sin moss.portable: bien, sin datos\'

# 4. Con moss.portable.
Copy-Item (Join-Path $dir 'moss.portable.ejemplo') (Join-Path $dir 'moss.portable')
Test-Moss
if (-not (Test-Path $datos)) { throw 'moss.exe con moss.portable no creó datos\' }
if ((Test-Rpc) -notmatch '"portable":true') { throw 'moss-rpc.exe con moss.portable no dijo portable:true' }
Remove-Item $datos -Recurse -Force

$gui = Start-Process -FilePath (Join-Path $dir 'moss-gui.exe') -WorkingDirectory $dir -PassThru

function Get-Rpc { Get-CimInstance Win32_Process -Filter "Name='moss-rpc.exe'" }

try {
    Wait-Until {
        if ($gui.HasExited) { throw "moss-gui.exe salió con $($gui.ExitCode)" }
        $null -ne (Get-Rpc)
    } 60 'moss-gui.exe arrancara moss-rpc.exe'
    # Sin comparar la ruta entera: el temporal del runner puede venir en 8.3.
    $running = (Get-Rpc).ExecutablePath
    if ($running -notlike "*José Pérez*\moss-$Version\moss-rpc.exe") {
        throw "moss-gui.exe arrancó otro moss-rpc.exe: $running"
    }
    Write-Output "moss-gui.exe arrancó el moss-rpc.exe de su carpeta: $running"
    Wait-Until { $gui.Refresh(); $gui.MainWindowHandle -ne [IntPtr]::Zero } 60 'saliera la ventana'
    Write-Output 'La ventana salió (primer cuadro)'
    Wait-Until { Test-Path $datos } 30 'se creara datos\'
    Write-Output 'datos\ está junto a los .exe'

    # La ventana se encoge hasta su mínimo: el contenido queda en 720×520 lógicos.
    Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class MossWindow {
    [StructLayout(LayoutKind.Sequential)] public struct Rect { public int L, T, R, B; }
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr w, IntPtr a, int x, int y, int cx, int cy, uint f);
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr w, out Rect r);
    [DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr w);
}
'@
    $window = $gui.MainWindowHandle
    $noMoveZOrderActivate = 0x2 -bor 0x4 -bor 0x10
    [void][MossWindow]::SetWindowPos($window, [IntPtr]::Zero, 0, 0, 100, 100, $noMoveZOrderActivate)
    $client = New-Object MossWindow+Rect
    [void][MossWindow]::GetClientRect($window, [ref]$client)
    $dpi = [MossWindow]::GetDpiForWindow($window)
    $expected = "$([math]::Floor(720 * $dpi / 96 + 0.5))x$([math]::Floor(520 * $dpi / 96 + 0.5))"
    $got = "$($client.R - $client.L)x$($client.B - $client.T)"
    Write-Output "Contenido al mínimo, a $dpi DPI: $got"
    if ($got -ne $expected) { throw "El mínimo debía ser $expected y fue $got" }

    if (-not $gui.CloseMainWindow()) { throw 'No se pudo mandar el cierre a la ventana' }
    Wait-Until { $gui.HasExited } 20 'moss-gui.exe saliera al cerrar la ventana'
    Wait-Until { $null -eq (Get-Rpc) } 10 'moss-rpc.exe saliera después de moss-gui.exe'
    Write-Output 'Al cerrar no quedó ningún moss-rpc.exe'
} finally {
    Get-Process moss-gui, moss-rpc -ErrorAction Ignore | Stop-Process -Force
}
Remove-Item $root -Recurse -Force
Write-Output "El ZIP de Moss ${Version}: todo bien"
