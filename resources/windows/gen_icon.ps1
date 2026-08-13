$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$size = 256
$bmp = New-Object System.Drawing.Bitmap($size, $size)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
$g.Clear([System.Drawing.Color]::FromArgb(88, 101, 242))
$font = New-Object System.Drawing.Font('Segoe UI', 150, [System.Drawing.FontStyle]::Bold, [System.Drawing.GraphicsUnit]::Pixel)
$sf = New-Object System.Drawing.StringFormat
$sf.Alignment = [System.Drawing.StringAlignment]::Center
$sf.LineAlignment = [System.Drawing.StringAlignment]::Center
$rect = New-Object System.Drawing.RectangleF(0, 0, $size, $size)
$g.DrawString('A', $font, [System.Drawing.Brushes]::White, $rect, $sf)
$g.Dispose()
$font.Dispose()
$ms = New-Object System.IO.MemoryStream
$bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
$png = $ms.ToArray()
$bmp.Dispose()
$dir = 'C:\Users\me\GIT\acheron\resources\windows'
$fs = [System.IO.File]::Create("$dir\app.ico")
$w = New-Object System.IO.BinaryWriter($fs)
$w.Write([UInt16]0)
$w.Write([UInt16]1)
$w.Write([UInt16]1)
$w.Write([Byte]0)
$w.Write([Byte]0)
$w.Write([Byte]0)
$w.Write([Byte]0)
$w.Write([UInt16]1)
$w.Write([UInt16]32)
$w.Write([UInt32]$png.Length)
$w.Write([UInt32]22)
$w.Write($png)
$w.Close()
$fs.Close()
Write-Output "app.ico written ($($png.Length) bytes)"
