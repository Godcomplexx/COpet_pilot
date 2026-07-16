param(
    [string]$InputDirectory = "animation",
    [string]$OutputFile = "main/assets/copet_animation_2bpp.bin"
)

$ErrorActionPreference = "Stop"

Add-Type -AssemblyName System.Drawing

$frames = Get-ChildItem -LiteralPath $InputDirectory -Filter "*.png" -File |
    Sort-Object Name

if ($frames.Count -eq 0) {
    throw "No PNG frames found in $InputDirectory"
}

$outputPath = [System.IO.Path]::GetFullPath($OutputFile)
$outputDirectory = [System.IO.Path]::GetDirectoryName($outputPath)
[System.IO.Directory]::CreateDirectory($outputDirectory) | Out-Null

$stream = [System.IO.File]::Open(
    $outputPath,
    [System.IO.FileMode]::Create,
    [System.IO.FileAccess]::Write)
$writer = [System.IO.BinaryWriter]::new($stream)

try {
    foreach ($frame in $frames) {
        $bitmap = [System.Drawing.Bitmap]::FromFile($frame.FullName)
        try {
            if ($bitmap.Width -ne 240 -or $bitmap.Height -ne 240) {
                throw "$($frame.Name) is $($bitmap.Width)x$($bitmap.Height); expected 240x240"
            }

            for ($y = 0; $y -lt 240; $y++) {
                for ($x = 0; $x -lt 240; $x += 4) {
                    $packed = 0
                    for ($pixel = 0; $pixel -lt 4; $pixel++) {
                        $color = $bitmap.GetPixel($x + $pixel, $y)
                        $alpha = [int]$color.A

                        # Composite transparency over white before converting to grayscale.
                        $red = [int](($color.R * $alpha + 255 * (255 - $alpha) + 127) / 255)
                        $green = [int](($color.G * $alpha + 255 * (255 - $alpha) + 127) / 255)
                        $blue = [int](($color.B * $alpha + 255 * (255 - $alpha) + 127) / 255)
                        $luminance = [int](($red * 77 + $green * 150 + $blue * 29 + 128) / 256)
                        $shade = [Math]::Min(3, [int](($luminance + 42) / 85))
                        $packed = $packed -bor ($shade -shl (6 - $pixel * 2))
                    }
                    $writer.Write([byte]$packed)
                }
            }
        }
        finally {
            $bitmap.Dispose()
        }
    }
}
finally {
    $writer.Dispose()
    $stream.Dispose()
}

$bytesPerFrame = 240 * 240 / 4
$totalBytes = (Get-Item -LiteralPath $outputPath).Length
Write-Host "Converted $($frames.Count) frames to $outputPath"
Write-Host "Format: 240x240, 2-bit grayscale, $bytesPerFrame bytes/frame, $totalBytes bytes total"
