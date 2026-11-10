# i18n yardimcisi - Qt'nin lupdate araci bu kurulumda calismiyor (Qt6Qml.dll
# yok), tr() dizgileri buraya ozel betikle cikarilir.
#
# --- Kullanim -------------------------------------------------------------
#   .\translations\i18n.ps1 extract   -> src/*.cpp'den tr() dizgilerini
#                                        sources.txt'e cikarir
#   .\translations\i18n.ps1 release   -> cevirileri i18n_en.ts ile birlestirip
#                                        resources/translations altina
#                                        i18n_en.qm derler (lrelease)
#
# Dosyalar:
#   sources.txt : idx|ctx|hash|satir| kaynak-dizgi  (gelistirici okur)
#   en-list.txt : idx<tab>ceviri                    (yeni ceviri yazmak icin;
#                  release bunu en.tsv'ye hash anahtariyla tasir ve silmez)
#   en.tsv      : hash<tab>ceviri                   (kalici ceviri deposu)
#
# ceviride @N@ -> yeni satir, @T@ -> tab olarak yazilir.
#
# YENI DIZGI EKLENDIGINDE:
#   1) .\translations\i18n.ps1 extract       (sources.txt guncellenir)
#   2) en-list.txt'e yeni dizgilerin cevirilerini yaz (idx<tab>ceviri)
#   3) .\translations\i18n.ps1 release

param(
    [Parameter(Position = 0)]
    [ValidateSet("extract", "release")]
    [string]$Mode = "extract"
)

$ErrorActionPreference = "Stop"

$scriptDir = $PSScriptRoot                 # translations/
$repoRoot  = Split-Path -Parent $scriptDir # xVNCManager/
$srcDir    = Join-Path $repoRoot "src"

$listFile   = Join-Path $scriptDir "sources.txt"
$authorFile = Join-Path $scriptDir "en-list.txt"
$tsvFile    = Join-Path $scriptDir "en.tsv"
$tsFile     = Join-Path $scriptDir "i18n_en.ts"
$qmFile     = Join-Path $repoRoot "resources\translations\i18n_en.qm"

# --- Qt araci (once $env:QTDIR, sonra komsu _toolchain) ---------------------
$cands = @()
if ($env:QTDIR) { $cands += (Join-Path (Join-Path $env:QTDIR "bin") "lrelease.exe") }
$cands += (Join-Path (Split-Path -Parent $repoRoot) "_toolchain\Qt\6.8.3\msvc2022_64\bin\lrelease.exe")
$lrelease = $null
foreach ($cand in $cands) {
    if (Test-Path $cand) { $lrelease = $cand; break }
}
if (-not $lrelease) { throw "lrelease.exe bulunamadi (QTDIR ya da _toolchain yolu)" }

# --- yardimci islevler -----------------------------------------------------
function Unescape-Cpp([string]$s) {
    $sb = New-Object System.Text.StringBuilder
    for ($i = 0; $i -lt $s.Length; $i++) {
        $c = $s[$i]
        if ($c -eq '\') {
            $i++
            if ($i -ge $s.Length) { [void]$sb.Append('\'); break }
            switch ([string]$s[$i]) {
                'n' { [void]$sb.Append("`n") }
                't' { [void]$sb.Append("`t") }
                'r' { [void]$sb.Append("`r") }
                '"' { [void]$sb.Append('"') }
                "'" { [void]$sb.Append("'") }
                '\' { [void]$sb.Append('\') }
                '0' { [void]$sb.Append([char]0) }
                'x' {
                    $j = $i + 1; $hx = ""
                    while ($j -lt $s.Length -and $hx.Length -lt 2 -and ([char]$s[$j]) -match '[0-9a-fA-F]') { $hx += $s[$j]; $j++ }
                    if ($hx) { [void]$sb.Append([convert]::ToChar([convert]::ToInt32($hx, 16))); $i = $j - 1 }
                    else     { [void]$sb.Append('x') }
                }
                default { [void]$sb.Append($s[$i]) }
            }
        } else {
            [void]$sb.Append($c)
        }
    }
    return $sb.ToString()
}

# sources.txt / en-list.txt tek satira donusturme
function Sanitize-Token([string]$s) {
    $s = $s.Replace("`r", "")
    $s = $s.Replace("`n", "@N@")
    $s = $s.Replace("`t", "@T@")
    return $s
}

function EscXml([string]$s) {
    $s = $s.Replace('&', '&amp;')
    $s = $s.Replace('<', '&lt;')
    $s = $s.Replace('>', '&gt;')
    return $s
}

function Extract-Data {
    $literal = '"(?:[^"\\]|\\.)*"'
    $pattern = ('(?<![A-Za-z0-9_])tr\s*\(\s*(' + $literal + '(?:\s+' + $literal + ')*)\s*\)')

    $msgs = New-Object "System.Collections.Generic.List[object]"
    foreach ($file in (Get-ChildItem $srcDir -Filter *.cpp | Sort-Object Name)) {
        $ctx = $file.BaseName
        $text = [IO.File]::ReadAllText($file.FullName, [Text.UTF8Encoding]::new($false))
        foreach ($m in [Regex]::Matches($text, $pattern, 'Multiline')) {
            $blob = $m.Groups[1].Value
            $src = ""
            foreach ($p in [Regex]::Matches($blob, $literal)) {
                $src += Unescape-Cpp ($p.Value.Substring(1, $p.Value.Length - 2))
            }
            $line = ([Regex]::Matches($text.Substring(0, $m.Index), "`n")).Count + 1
            $msgs.Add([pscustomobject]@{ ctx = $ctx; file = $file.Name; line = $line; src = $src })
        }
    }
    return $msgs
}

function Get-Key([string]$ctx, [string]$src) {
    # ctx+source anahtari: index kaymalari gelecek eklemelerde zarar vermez
    return ([string]$ctx + [char]1 + $src).GetHashCode().ToString("X")
}

function Get-Unique {
    $msgs = Extract-Data
    $seen = @{}
    $unique = New-Object "System.Collections.Generic.List[object]"
    foreach ($m in $msgs) {
        $key = Get-Key $m.ctx $m.src
        if ($seen.ContainsKey($key)) { continue }
        $seen[$key] = $true
        $unique.Add([pscustomobject]@{ idx = ($unique.Count + 1); ctx = $m.ctx; file = $m.file; line = $m.line; src = $m.src; key = $key })
    }
    return ,$unique
}

if ($Mode -eq "extract") {
    $unique = Get-Unique
    $lines = @("index|ctx|hash|satir|kaynak (@N@ yeni satir, @T@ tab)")
    foreach ($m in $unique) {
        $lines += ("{0}|{1}|{2}|{3}|{4}" -f $m.idx, $m.ctx, $m.key, $m.line, (Sanitize-Token $m.src))
    }
    [IO.File]::WriteAllLines($listFile, $lines, [Text.UTF8Encoding]::new($false))
    "TOPLAM benzersiz dizgi: $($unique.Count)  ->  $listFile"
}

if ($Mode -eq "release") {
    $unique = Get-Unique

    # 1) en-list.txt (idx -> ceviri, yazim dosyasi)
    $author = @{}
    if (Test-Path $authorFile) {
        foreach ($line in [IO.File]::ReadAllLines($authorFile)) {
            if ([string]::IsNullOrWhiteSpace($line) -or $line.StartsWith("#")) { continue }
            $t = $line.IndexOf("`t")
            if ($t -le 0) { continue }
            $author[$line.Substring(0, $t).Trim()] = $line.Substring($t + 1)
        }
    }

    # 2) en.tsv (hash -> ceviri, kalici depo) + en-list'ten guncelle/yaz
    $tsv = @{}
    if (Test-Path $tsvFile) {
        foreach ($line in [IO.File]::ReadAllLines($tsvFile)) {
            if ([string]::IsNullOrWhiteSpace($line) -or $line.StartsWith("#")) { continue }
            $t = $line.IndexOf("`t")
            if ($t -le 0) { continue }
            $tsv[$line.Substring(0, $t)] = $line.Substring($t + 1)
        }
    }
    $lines = @("# i18n_en cevirileri - hash<tab>ceviri  (@N@ yeni satir, @T@ tab)")
    foreach ($m in $unique) {
        if ($author.ContainsKey([string]$m.idx)) {
            $tsv[$m.key] = $author[[string]$m.idx]
        }
        $lines += ($m.key + "`t" + $tsv[$m.key])
    }
    [IO.File]::WriteAllLines($tsvFile, $lines, [Text.UTF8Encoding]::new($false))

    # 3) .ts yaz (cevirsiz olanlar unfinished -> Ingilizce modda Turkce gorunur)
    $sb = New-Object System.Text.StringBuilder
    [void]$sb.AppendLine('<?xml version="1.0" encoding="utf-8"?>')
    [void]$sb.AppendLine('<!DOCTYPE TS>')
    [void]$sb.AppendLine('<TS version="2.1" language="en" sourcelanguage="tr">')
    $missing = New-Object "System.Collections.Generic.List[string]"
    foreach ($m in ($unique | Group-Object ctx | Sort-Object Name)) {
        [void]$sb.AppendLine("    <context>")
        [void]$sb.AppendLine("        <name>" + $m.Name + "</name>")
        foreach ($u in $m.Group) {
            [void]$sb.AppendLine("        <message>")
            [void]$sb.AppendLine("            <source>" + (EscXml $u.src) + "</source>")
            $tr = $null
            if ($tsv.ContainsKey($u.key)) {
                $tr = $tsv[$u.key].Replace("@N@", "`n").Replace("@T@", "`t")
            }
            if ([string]::IsNullOrEmpty($tr)) {
                [void]$sb.AppendLine("            <translation type=`"unfinished`"></translation>")
                $missing.Add(($u.ctx + " :: " + ((Sanitize-Token $u.src).Substring(0, [Math]::Min(60, $u.src.Length)))))
            } else {
                [void]$sb.AppendLine("            <translation>" + (EscXml $tr) + "</translation>")
            }
            [void]$sb.AppendLine("        </message>")
        }
        [void]$sb.AppendLine("    </context>")
    }
    [void]$sb.AppendLine("</TS>")
    [IO.File]::WriteAllText($tsFile, $sb.ToString(), [Text.UTF8Encoding]::new($false))

    # 4) lrelease -> resources/translations/i18n_en.qm
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $qmFile) | Out-Null
    & $lrelease $tsFile -qm $qmFile | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "lrelease basarisiz" }

    "QM : $qmFile ($((Get-Item $qmFile).Length) bayt)"
    "CEVIRI: $(($unique.Count - $missing.Count)) / $($unique.Count)"
    if ($missing.Count -gt 0) {
        "CEVRISIZ dizgiler (Ingilizce modda Turkce gorunecek):"
        $missing
    }
}
