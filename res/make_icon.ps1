# Gera res\app.ico (mira ciana em fundo escuro) com PNGs embutidos
Add-Type -AssemblyName System.Drawing
$sizes = 256, 64, 48, 32, 16
$pngs = @()
foreach ($sz in $sizes) {
    $bmp = New-Object System.Drawing.Bitmap $sz, $sz
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = 'AntiAlias'
    $g.Clear([System.Drawing.Color]::Transparent)
    $s = $sz / 32.0
    $r = 7 * $s; $d = $r * 2
    $path = New-Object System.Drawing.Drawing2D.GraphicsPath
    $path.AddArc(0, 0, $d, $d, 180, 90)
    $path.AddArc($sz - $d - 1, 0, $d, $d, 270, 90)
    $path.AddArc($sz - $d - 1, $sz - $d - 1, $d, $d, 0, 90)
    $path.AddArc(0, $sz - $d - 1, $d, $d, 90, 90)
    $path.CloseFigure()
    $g.FillPath((New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(255, 9, 9, 11))), $path)
    $cyan = [System.Drawing.Color]::FromArgb(255, 250, 250, 250)
    $pen = New-Object System.Drawing.Pen $cyan, ([Math]::Max(1.2, 2.2 * $s))
    $g.DrawEllipse($pen, 8 * $s, 8 * $s, 16 * $s, 16 * $s)
    $g.DrawLine($pen, 16 * $s, 3 * $s, 16 * $s, 10 * $s)
    $g.DrawLine($pen, 16 * $s, 22 * $s, 16 * $s, 29 * $s)
    $g.DrawLine($pen, 3 * $s, 16 * $s, 10 * $s, 16 * $s)
    $g.DrawLine($pen, 22 * $s, 16 * $s, 29 * $s, 16 * $s)
    $g.FillEllipse((New-Object System.Drawing.SolidBrush $cyan), 14 * $s, 14 * $s, 4 * $s, 4 * $s)
    $g.Dispose()
    $ms = New-Object System.IO.MemoryStream
    $bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
    $pngs += , $ms.ToArray()
    $bmp.Dispose()
}
$out = New-Object System.IO.MemoryStream
$w = New-Object System.IO.BinaryWriter $out
$w.Write([UInt16]0); $w.Write([UInt16]1); $w.Write([UInt16]$sizes.Count)
$offset = 6 + 16 * $sizes.Count
for ($i = 0; $i -lt $sizes.Count; $i++) {
    $sz = $sizes[$i]; $b = if ($sz -ge 256) { 0 } else { $sz }
    $w.Write([byte]$b); $w.Write([byte]$b); $w.Write([byte]0); $w.Write([byte]0)
    $w.Write([UInt16]1); $w.Write([UInt16]32)
    $w.Write([UInt32]$pngs[$i].Length); $w.Write([UInt32]$offset)
    $offset += $pngs[$i].Length
}
foreach ($p in $pngs) { $w.Write($p) }
[System.IO.File]::WriteAllBytes((Join-Path $PSScriptRoot 'app.ico'), $out.ToArray())
