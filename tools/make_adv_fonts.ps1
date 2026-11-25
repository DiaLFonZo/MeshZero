# Cardputer ADV: pre-drawn Montserrat fonts (kept in flash, no TrueType drawing at run time).
# Sizes are the Zero sizes at 4/5: 12->10, 14->11, 16->13, 20->16, 46->37 (clock digits only).
# Needs Node.js (npx downloads lv_font_conv). Run from anywhere; writes src/ui/fonts/adv/*.c
$ErrorActionPreference = 'Stop'
$root  = Split-Path -Parent $PSScriptRoot
$fonts = Join-Path $root 'assets\fonts'
$out   = Join-Path $root 'src\ui\fonts\adv'
New-Item -ItemType Directory -Force $out | Out-Null
$text  = '0x20-0x7E,0xA0-0xFF,0x2013-0x2014,0x2018-0x201D,0x2022,0x2026'
$clock = '0x20,0x30-0x3A,0x41,0x4D,0x50'
$jobs = @()
foreach ($s in 10, 11, 13, 16) {
    $jobs += , @("mc_adv_medium_$s", 'Montserrat-Medium.ttf', $s, $text)
    $jobs += , @("mc_adv_bold_$s",   'Montserrat-Bold.ttf',   $s, $text)
}
$jobs += , @('mc_adv_bold_37', 'Montserrat-Bold.ttf', 37, $clock)
foreach ($j in $jobs) {
    $name, $file, $size, $range = $j
    & npx --yes lv_font_conv@1.5.2 --no-compress --bpp 4 --size $size --format lvgl `
        --font (Join-Path $fonts $file) -r $range `
        --lv-include 'lvgl.h' -o (Join-Path $out "$name.c")
    if ($LASTEXITCODE -ne 0) { throw "lv_font_conv failed for $name" }
}
Get-ChildItem $out | Select-Object Name, Length
