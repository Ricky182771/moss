# Prueba una carpeta de tools/package-gui-test-windows.ps1 como en una PC sin
# MSYS2 (corre en la CI con pwsh 7). Sobre una copia en una ruta con espacios y
# acentos, con moss.portable y solo Windows en el PATH:
#
#   tools/check-gui-test-windows.ps1 -Source <carpeta> [-FrameStats]
#
# 1. moss-rpc.exe responde initialize, shutdown y exit, con portable:true: no
#    falta ninguna DLL.
# 2. moss-gui.exe abre su ventana (primer cuadro) y arranca el moss-rpc.exe que
#    está a su lado, que crea datos\.
# 3. Al cerrar la ventana no queda ningún moss-rpc.exe vivo.
# Con -FrameStats (solo profile), además, que --frame-stats escriba en un stderr
# redirigido, como se mide en la escuela.
param(
    [Parameter(Mandatory)][string]$Source,
    [switch]$FrameStats
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
$dir = Join-Path $root 'moss gui'
New-Item -ItemType Directory $root | Out-Null
Copy-Item $Source $dir -Recurse
New-Item -ItemType File (Join-Path $dir 'moss.portable') | Out-Null
$env:PATH = "$env:windir\System32;$env:windir"

$rpcPath = Join-Path $dir 'moss-rpc.exe'
$in = Join-Path $root 'rpc-in.txt'
$out = Join-Path $root 'rpc-out.txt'
$err = Join-Path $root 'rpc-err.txt'
@(
    '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocol_version":1,"client":{"name":"ci","version":"0"}}}'
    '{"jsonrpc":"2.0","id":2,"method":"shutdown","params":{}}'
    '{"jsonrpc":"2.0","method":"exit"}'
) | Set-Content -Encoding utf8NoBOM $in
$rpc = Start-Process -FilePath $rpcPath -WorkingDirectory $dir -NoNewWindow -PassThru -Wait `
    -RedirectStandardInput $in -RedirectStandardOutput $out -RedirectStandardError $err
$reply = Get-Content -Raw -Encoding utf8 $out
Write-Output "moss-rpc.exe: código de salida $($rpc.ExitCode)"
if ($rpc.ExitCode -ne 0) {
    Get-Content $err
    throw "moss-rpc.exe salió con $($rpc.ExitCode) (-1073741515 es una DLL que falta)"
}
if ($reply -notmatch '"portable":true') { throw "initialize sin portable:true: $reply" }
if ($reply -notmatch '"jsonrpc":"2.0","id":2,"result":\{\}') { throw "shutdown no respondió: $reply" }
Remove-Item (Join-Path $dir 'datos') -Recurse -Force -ErrorAction Ignore

$start = @{ FilePath = (Join-Path $dir 'moss-gui.exe'); WorkingDirectory = $dir; PassThru = $true }
$stats = Join-Path $root 'stats.txt'
if ($FrameStats) {
    $start.ArgumentList = '--frame-stats'
    $start.RedirectStandardError = $stats
}
$gui = Start-Process @start

function Get-Rpc { Get-CimInstance Win32_Process -Filter "Name='moss-rpc.exe'" }

try {
    Wait-Until {
        if ($gui.HasExited) { throw "moss-gui.exe salió con $($gui.ExitCode)" }
        $null -ne (Get-Rpc)
    } 60 'moss-gui.exe arrancara moss-rpc.exe'
    # Sin comparar la ruta entera: el temporal del runner puede venir en 8.3.
    $running = (Get-Rpc).ExecutablePath
    if ($running -notlike '*José Pérez*\moss gui\moss-rpc.exe') {
        throw "moss-gui.exe arrancó otro moss-rpc.exe: $running"
    }
    Write-Output "moss-gui.exe arrancó el moss-rpc.exe de su carpeta: $running"
    Wait-Until { $gui.Refresh(); $gui.MainWindowHandle -ne [IntPtr]::Zero } 60 'saliera la ventana'
    Write-Output 'La ventana salió (primer cuadro)'
    Wait-Until { Test-Path (Join-Path $dir 'datos') } 30 'se creara datos\'
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

if ($FrameStats) {
    $lines = @(Get-Content -Encoding utf8 $stats | Where-Object { $_ -match '^frame-stats:' })
    $lines | Select-Object -First 3
    if (-not $lines) { throw '--frame-stats no escribió nada en el stderr redirigido' }
}
Remove-Item $root -Recurse -Force
