<?php
/*
 * Pilon Map Downloader – proxy pre mapové služby
 *
 * Mapové servery ÚGKK SR a hlavného mesta Bratislavy nepovoľujú sťahovanie údajov priamo
 * z prehliadača (CORS), preto ich nástroj načítava cez tento skript na vašom webhostingu.
 * Povolené sú len adresy zo zoznamu $ALLOWED_HOSTS (žiadne iné servery, len GET, len HTTPS).
 */

// Voliteľný prístupový kľúč – ak ho vyplníte, zadajte ho aj v nástroji (⚙ › Služby › Kľúč proxy).
$ACCESS_KEY = '';

$ALLOWED_HOSTS = [
    'zbgisws.skgeodesy.sk',
    'inspirews.skgeodesy.sk',
    'kataster.skgeodesy.sk',
    'geoportal.bratislava.sk',
];

$MAX_BYTES = 300 * 1024 * 1024;   // max. veľkosť jednej odpovede
$TIMEOUT = 280;                    // s

header('X-PMD-Proxy: 1');
header('X-Content-Type-Options: nosniff');

function fail($code, $msg) {
    http_response_code($code);
    header('Content-Type: text/plain; charset=utf-8');
    echo $msg;
    exit;
}

if ($_SERVER['REQUEST_METHOD'] !== 'GET') fail(405, 'Povolená je len metóda GET');
if ($ACCESS_KEY !== '' && !hash_equals($ACCESS_KEY, (string)($_GET['key'] ?? ''))) fail(403, 'Neplatný kľúč proxy');
if (!function_exists('curl_init')) fail(500, 'Na serveri chýba rozšírenie PHP cURL');

function allowed_url($url, $hosts) {
    $p = parse_url($url);
    if (!$p || strtolower($p['scheme'] ?? '') !== 'https') return false;
    if (isset($p['user']) || isset($p['pass']) || isset($p['port'])) return false;
    return in_array(strtolower($p['host'] ?? ''), $hosts, true);
}

$url = (string)($_GET['url'] ?? '');
if ($url === '' || strlen($url) > 8000 || !allowed_url($url, $ALLOWED_HOSTS)) fail(403, 'Nepovolená adresa');

@set_time_limit($TIMEOUT + 20);
@ini_set('zlib.output_compression', '0');
while (ob_get_level() > 0) ob_end_clean();

$redirects = 0;
while (true) {
    $status = 0;
    $ctype = 'application/octet-stream';
    $location = null;
    $sent = false;
    $total = 0;

    $ch = curl_init($url);
    curl_setopt_array($ch, [
        CURLOPT_FOLLOWLOCATION => false,
        CURLOPT_CONNECTTIMEOUT => 20,
        CURLOPT_TIMEOUT => $TIMEOUT,
        CURLOPT_ENCODING => '',
        CURLOPT_USERAGENT => 'PilonMapDownloader/1.0 (+proxy)',
        CURLOPT_HTTPHEADER => ['Accept: */*'],
        CURLOPT_PROTOCOLS => CURLPROTO_HTTPS,
        CURLOPT_HEADERFUNCTION => function ($ch, $h) use (&$status, &$ctype, &$location) {
            if (preg_match('#^HTTP/\S+\s+(\d{3})#', $h, $m)) { $status = (int)$m[1]; $location = null; }
            elseif (stripos($h, 'content-type:') === 0) $ctype = trim(substr($h, 13));
            elseif (stripos($h, 'location:') === 0) $location = trim(substr($h, 9));
            return strlen($h);
        },
        CURLOPT_WRITEFUNCTION => function ($ch, $data) use (&$status, &$ctype, &$sent, &$total, $MAX_BYTES) {
            if ($status >= 300 && $status < 400) return strlen($data);
            if (!$sent) {
                http_response_code($status ?: 502);
                header('Content-Type: ' . $ctype);
                header('Cache-Control: private, max-age=300');
                $sent = true;
            }
            $total += strlen($data);
            if ($total > $MAX_BYTES) return 0;
            echo $data;
            flush();
            return strlen($data);
        },
    ]);
    $ok = curl_exec($ch);
    $err = curl_error($ch);
    curl_close($ch);

    if ($status >= 300 && $status < 400 && $location) {
        if (++$redirects > 3) fail(502, 'Príliš veľa presmerovaní');
        if (!preg_match('#^https?://#i', $location)) {
            $p = parse_url($url);
            $location = 'https://' . $p['host'] . ($location[0] === '/' ? '' : '/') . $location;
        }
        if (!allowed_url($location, $ALLOWED_HOSTS)) fail(403, 'Presmerovanie na nepovolenú adresu');
        $url = $location;
        continue;
    }
    if (!$sent) {
        if ($ok === false) fail(502, 'Server neodpovedá: ' . $err);
        http_response_code($status ?: 502);
        header('Content-Type: ' . $ctype);
    }
    break;
}
