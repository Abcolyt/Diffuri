<#
.SYNOPSIS
    Apply substitutions from a table (delimiter: |||).

.PARAMETER Table
    Path to replacements.txt (UTF-8).

.PARAMETER Apply
    Without this flag, dry-run. With it, actually writes files.

.USAGE
    .\apply-translations.ps1 -Table replacements.txt
    .\apply-translations.ps1 -Table replacements.txt -Apply
#>
[CmdletBinding()]
param(
    [string]$Table = "replacements.txt",
    [switch]$Apply
)

$ErrorActionPreference = "Stop"
$utf8NoBom = New-Object System.Text.UTF8Encoding($false)

if (-not (Test-Path $Table)) {
    Write-Error "Table not found: $Table"
    return
}

$lines = [System.IO.File]::ReadAllLines($Table, $utf8NoBom)
if ($lines.Length -lt 2) {
    Write-Error "Empty table"
    return
}

$rows = @()
for ($i = 1; $i -lt $lines.Length; $i++) {
    $line = $lines[$i]
    if ([string]::IsNullOrWhiteSpace($line)) { continue }
    $parts = $line -split "\|\|\|", 3
    if ($parts.Length -lt 3) {
        Write-Warning ("Malformed row {0}: {1}" -f ($i + 1), $line)
        continue
    }
    $rows += [pscustomobject]@{
        File = $parts[0].Trim()
        Old  = $parts[1]
        New  = $parts[2]
    }
}

Write-Host ("Loaded {0} replacement rules from {1}" -f $rows.Count, $Table) -ForegroundColor Cyan

$byFile = @{}
$fileOrder = @()
foreach ($r in $rows) {
    if (-not $byFile.ContainsKey($r.File)) {
        $byFile[$r.File] = New-Object System.Collections.ArrayList
        $fileOrder += $r.File
    }
    [void]$byFile[$r.File].Add($r)
}

$totalMatches = 0

foreach ($file in $fileOrder) {
    if (-not (Test-Path $file)) {
        Write-Warning ("File not found: {0} - skipped" -f $file)
        continue
    }

    $text = [System.IO.File]::ReadAllText($file, $utf8NoBom)
    $fileMatches = 0

    foreach ($r in $byFile[$file]) {
        if ([string]::IsNullOrEmpty($r.Old)) { continue }
        $count = ([regex]::Matches($text, [regex]::Escape($r.Old))).Count
        if ($count -gt 0) {
            $text = $text.Replace($r.Old, $r.New)
            $fileMatches += $count
        }
    }

    if ($fileMatches -gt 0) {
        $totalMatches += $fileMatches
        if ($Apply) {
            [System.IO.File]::WriteAllText($file, $text, $utf8NoBom)
            Write-Host ("  WRITE  {0,-55} {1,4} hits" -f $file, $fileMatches) -ForegroundColor Green
        } else {
            Write-Host ("  DRY    {0,-55} {1,4} hits" -f $file, $fileMatches) -ForegroundColor Yellow
        }
    } else {
        Write-Host ("  --     {0,-55} no matches" -f $file)
    }
}

Write-Host ""
Write-Host ("Total hits: {0}" -f $totalMatches)
if (-not $Apply) {
    Write-Host "This was a DRY RUN. Re-run with -Apply to write files." -ForegroundColor Yellow
} else {
    Write-Host "Done. Review with: git diff" -ForegroundColor Cyan
}