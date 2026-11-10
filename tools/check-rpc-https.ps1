# Una conversación de verdad por HTTPS con la curl de Windows (Schannel), sin
# red ni keys (corre en la CI con pwsh 7):
#
#   tools/check-rpc-https.ps1 -Rpc <moss-rpc.exe>
#
# Crea un certificado para localhost, lo agrega a las raíces de confianza de la
# máquina (por eso necesita administrador: el runner lo es) y arranca
# tools/fake-llm.py con --tls, que acepta hasta TLS 1.2, como Schannel en
# Windows 10. Luego, con una copia de moss-rpc.exe y moss.portable (sin tocar
# el AppData), initialize, chat.send y chat.finished con outcome ok y el texto
# del servidor falso. Si NATIVE_CA o REVOKE_BEST_EFFORT dieran error de setopt,
# o el certificado autofirmado (sin CRL) no pasara, falla aquí.
param(
    [Parameter(Mandatory)][string]$Rpc
)
$ErrorActionPreference = 'Stop'
$port = 8443
$repo = Split-Path $PSScriptRoot

$work = Join-Path ([IO.Path]::GetTempPath()) "moss-https-$([guid]::NewGuid())"
New-Item -ItemType Directory $work | Out-Null
Copy-Item $Rpc $work
New-Item -ItemType File (Join-Path $work 'moss.portable') | Out-Null

# Certificado autofirmado para localhost, solo para esta prueba (un día).
$rsa = [Security.Cryptography.RSA]::Create(2048)
$request = [Security.Cryptography.X509Certificates.CertificateRequest]::new(
    'CN=localhost', $rsa, [Security.Cryptography.HashAlgorithmName]::SHA256,
    [Security.Cryptography.RSASignaturePadding]::Pkcs1)
$san = [Security.Cryptography.X509Certificates.SubjectAlternativeNameBuilder]::new()
$san.AddDnsName('localhost')
$request.CertificateExtensions.Add($san.Build())
$cert = $request.CreateSelfSigned((Get-Date).AddMinutes(-5), (Get-Date).AddDays(1))
$certPem = Join-Path $work 'cert.pem'
$keyPem = Join-Path $work 'key.pem'
$certDer = Join-Path $work 'cert.cer'
Set-Content -Encoding ascii $certPem $cert.ExportCertificatePem()
Set-Content -Encoding ascii $keyPem $rsa.ExportPkcs8PrivateKeyPem()
[IO.File]::WriteAllBytes($certDer, $cert.Export('Cert'))
Import-Certificate -FilePath $certDer -CertStoreLocation Cert:\LocalMachine\Root | Out-Null

$server = Start-Process -FilePath python -PassThru -NoNewWindow `
    -ArgumentList '-I', (Join-Path $repo 'tools/fake-llm.py'), '--port', $port, '--tls', $certPem, $keyPem `
    -RedirectStandardError (Join-Path $work 'fake-llm.txt')

$process = $null
try {
    $limit = (Get-Date).AddSeconds(20)
    while (-not (Test-NetConnection 127.0.0.1 -Port $port -InformationLevel Quiet -WarningAction Ignore)) {
        if ($server.HasExited -or (Get-Date) -gt $limit) {
            Get-Content (Join-Path $work 'fake-llm.txt')
            throw 'fake-llm.py no arrancó'
        }
        Start-Sleep -Milliseconds 250
    }

    foreach ($name in 'CHAT_DATA_DIR', 'CHAT_DOWNLOAD_DIR', 'CHAT_SEARCH_API_KEY') {
        Remove-Item "env:$name" -ErrorAction Ignore
    }
    $env:CHAT_BASE_URL = "https://localhost:$port/v1"
    $env:CHAT_API_KEY = 'ci-sin-key-real'
    $env:CHAT_MODEL = 'fake'

    $info = [Diagnostics.ProcessStartInfo]::new((Join-Path $work 'moss-rpc.exe'))
    $info.WorkingDirectory = $work
    $info.UseShellExecute = $false
    $info.RedirectStandardInput = $true
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    $info.StandardOutputEncoding = [Text.UTF8Encoding]::new($false)
    $process = [Diagnostics.Process]::Start($info)
    $errors = $process.StandardError.ReadToEndAsync()
    $stdin = $process.StandardInput
    $stdin.AutoFlush = $true

    # Lee líneas hasta una que cumpla $Pattern; las devuelve todas.
    function Read-Until([string]$Pattern, [int]$Seconds) {
        $lines = @()
        $limit = (Get-Date).AddSeconds($Seconds)
        while ($true) {
            $left = [int]($limit - (Get-Date)).TotalMilliseconds
            $next = $process.StandardOutput.ReadLineAsync()
            if ($left -le 0 -or -not $next.Wait($left) -or $null -eq $next.Result) {
                $lines | ForEach-Object { $_.Substring(0, [math]::Min(300, $_.Length)) }
                throw "moss-rpc.exe no mandó $Pattern"
            }
            $lines += $next.Result
            if ($next.Result -match $Pattern) { return $lines }
        }
    }

    $stdin.WriteLine('{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocol_version":1,"client":{"name":"ci","version":"0"}}}')
    $init = @(Read-Until '"id":1,' 20)[-1]
    if ($init -notmatch '"ready":true') { throw "initialize sin ready:true: $init" }
    if ($init -notmatch '"conversation":"([^"]+)"') { throw "initialize sin conversation: $init" }
    $conversation = $Matches[1]

    $stdin.WriteLine('{"jsonrpc":"2.0","id":2,"method":"chat.send","params":{"conversation":"' +
        $conversation + '","text":"hola"}}')
    $events = @(Read-Until '"method":"chat.finished"' 60)
    $finished = $events[-1]
    Write-Output $finished
    if ($finished -notmatch '"outcome":"ok"') { throw "chat.finished no fue ok: $finished" }
    if (-not ($events -match '"kind":"assistant".*selection sort')) {
        throw 'La respuesta no trae el texto del servidor falso'
    }
    Write-Output 'Conversación por HTTPS (TLS 1.2, Schannel) con el servidor falso: bien'

    $stdin.WriteLine('{"jsonrpc":"2.0","id":3,"method":"shutdown","params":{}}')
    $stdin.WriteLine('{"jsonrpc":"2.0","method":"exit"}')
    if (-not $process.WaitForExit(10000)) { throw 'moss-rpc.exe no salió después de exit' }
    if ($process.ExitCode -ne 0) { throw "moss-rpc.exe salió con $($process.ExitCode)" }
} catch {
    if ($process) { $process.Kill(); $errors.Result }
    Get-Content (Join-Path $work 'fake-llm.txt') -ErrorAction Ignore
    throw
} finally {
    Stop-Process -Id $server.Id -Force -ErrorAction Ignore
    Get-ChildItem Cert:\LocalMachine\Root | Where-Object Thumbprint -eq $cert.Thumbprint | Remove-Item
}
Remove-Item $work -Recurse -Force
