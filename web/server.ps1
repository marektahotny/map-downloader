# Pilon Map Downloader - local server (Windows PowerShell 5.1+, no installation needed)
# Serves the web tool on http://localhost:8765/ and forwards map-service requests (proxy.php?url=...)
# only to the whitelisted map servers of UGKK SR and the City of Bratislava.

param([int]$Port = 8765)

$ErrorActionPreference = 'Stop'
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
Add-Type -AssemblyName System.Web

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$allowed = @('zbgisws.skgeodesy.sk', 'inspirews.skgeodesy.sk', 'kataster.skgeodesy.sk', 'geoportal.bratislava.sk')
$types = @{
    '.html' = 'text/html; charset=utf-8'; '.js' = 'application/javascript; charset=utf-8'; '.css' = 'text/css; charset=utf-8'
    '.png' = 'image/png'; '.svg' = 'image/svg+xml'; '.json' = 'application/json'; '.txt' = 'text/plain; charset=utf-8'; '.ico' = 'image/x-icon'
}

function Test-Allowed([string]$u) {
    try { $x = [Uri]$u } catch { return $false }
    return ($x.Scheme -eq 'https' -and $x.IsDefaultPort -and [string]::IsNullOrEmpty($x.UserInfo) -and ($allowed -contains $x.Host.ToLower()))
}

function Send-Text($resp, [int]$code, [string]$text) {
    $resp.StatusCode = $code
    $resp.ContentType = 'text/plain; charset=utf-8'
    $b = [Text.Encoding]::UTF8.GetBytes($text)
    $resp.OutputStream.Write($b, 0, $b.Length)
}

function Invoke-Proxy($ctx) {
    $resp = $ctx.Response
    $resp.Headers.Add('X-PMD-Proxy', '1')
    $url = [Web.HttpUtility]::ParseQueryString($ctx.Request.Url.Query)['url']
    if (-not $url -or -not (Test-Allowed $url)) { Send-Text $resp 403 'Nepovolena adresa'; return }
    for ($i = 0; $i -le 3; $i++) {
        $req = [Net.HttpWebRequest]::Create($url)
        $req.AllowAutoRedirect = $false
        $req.Timeout = 300000
        $req.ReadWriteTimeout = 300000
        $req.UserAgent = 'PilonMapDownloader/1.0 (+local)'
        $req.AutomaticDecompression = [Net.DecompressionMethods]::GZip -bor [Net.DecompressionMethods]::Deflate
        try { $r = $req.GetResponse() }
        catch [Net.WebException] {
            $r = $_.Exception.Response
            if (-not $r) { Send-Text $resp 502 ('Server neodpoveda: ' + $_.Exception.Message); return }
        }
        $code = [int]$r.StatusCode
        if ($code -ge 300 -and $code -lt 400 -and $r.Headers['Location']) {
            $next = [Uri]::new([Uri]$url, $r.Headers['Location']).AbsoluteUri
            $r.Close()
            if (-not (Test-Allowed $next)) { Send-Text $resp 403 'Presmerovanie na nepovolenu adresu'; return }
            $url = $next
            continue
        }
        $resp.StatusCode = $code
        if ($r.ContentType) { $resp.ContentType = $r.ContentType }
        $s = $r.GetResponseStream()
        $s.CopyTo($resp.OutputStream)
        $s.Close()
        $r.Close()
        return
    }
    Send-Text $resp 502 'Prilis vela presmerovani'
}

function Invoke-Static($ctx) {
    $resp = $ctx.Response
    $rel = [Uri]::UnescapeDataString($ctx.Request.Url.AbsolutePath).TrimStart('/')
    if ($rel -eq '') { $rel = 'index.html' }
    $full = [IO.Path]::GetFullPath((Join-Path $root $rel))
    if (-not $full.StartsWith($root.TrimEnd([IO.Path]::DirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase) -or -not (Test-Path $full -PathType Leaf)) { Send-Text $resp 404 'Nenajdene'; return }
    $ext = [IO.Path]::GetExtension($full).ToLower()
    $resp.ContentType = $(if ($types.ContainsKey($ext)) { $types[$ext] } else { 'application/octet-stream' })
    $resp.Headers.Add('Cache-Control', 'no-cache')
    $b = [IO.File]::ReadAllBytes($full)
    $resp.ContentLength64 = $b.Length
    $resp.OutputStream.Write($b, 0, $b.Length)
}

$listener = New-Object Net.HttpListener
$listener.Prefixes.Add("http://localhost:$Port/")
try { $listener.Start() }
catch {
    Write-Host "Port $Port je obsadeny alebo nedostupny: $($_.Exception.Message)" -ForegroundColor Red
    Read-Host 'Enter = koniec'
    exit 1
}
$addr = "http://localhost:$Port/"
Write-Host ''
Write-Host '  Pilon Map Downloader bezi na ' -NoNewline; Write-Host $addr -ForegroundColor Green
Write-Host '  Okno nechajte otvorene pocas prace. Ukoncenie: zatvorte okno alebo Ctrl+C.'
Write-Host ''
Start-Process $addr

while ($listener.IsListening) {
    $ctx = $listener.GetContext()
    try {
        if ($ctx.Request.Url.AbsolutePath -eq '/proxy.php') { Invoke-Proxy $ctx } else { Invoke-Static $ctx }
    } catch {
        try { Send-Text $ctx.Response 500 $_.Exception.Message } catch { }
    } finally {
        try { $ctx.Response.Close() } catch { }
    }
}
