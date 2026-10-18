param(
  [Parameter(Mandatory = $true)]
  [string]$ConfigPath,
  [Parameter(Mandatory = $true)]
  [string]$VfsFramePath,
  [int]$FrameWidth = 512,
  [int]$FrameHeight = 1024,
  [int]$FrameIntervalMs = 100,
  [int]$RamDiskSizeMB = 64,
  [int]$JpegQuality = 92,
  [string]$FrameName = 'android_frame.jpg',
  [string]$MetadataName = 'android_frame_state.json',
  [int]$Cycles = 0
)

$ErrorActionPreference = 'Stop'

function Assert-Administrator {
  $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
  $principal = [Security.Principal.WindowsPrincipal]::new($identity)
  if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Run this diagnostic producer from an elevated PowerShell.'
  }
}

function Publish-AtomicFile {
  param([string]$TemporaryPath, [string]$TargetPath)
  if ([IO.File]::Exists($TargetPath)) {
    $backupPath = "$TargetPath.bak"
    Remove-Item -LiteralPath $backupPath -Force -ErrorAction SilentlyContinue
    [IO.File]::Replace($TemporaryPath, $TargetPath, $backupPath, $true)
    Remove-Item -LiteralPath $backupPath -Force -ErrorAction SilentlyContinue
  } else {
    [IO.File]::Move($TemporaryPath, $TargetPath)
  }
}

function Write-JsonAtomically {
  param([string]$Path, [object]$Value)
  $temporary = "$Path.tmp"
  $json = $Value | ConvertTo-Json -Compress -Depth 5
  $utf8 = [Text.UTF8Encoding]::new($false)
  [IO.File]::WriteAllText($temporary, $json, $utf8)
  # The initial handshake is published once. Windows PowerShell 5.1 can throw
  # ArgumentException for File.Replace(..., $null, ...), so do not repeatedly
  # replace a JSON file that BeamNG may be reading. Frame files use the separate
  # atomic .tmp -> replace path below.
  if ([IO.File]::Exists($Path)) {
    Remove-Item -LiteralPath $Path -Force -ErrorAction Stop
  }
  [IO.File]::Move($temporary, $Path)
  return $true
}

function Write-JsonFrameMetadata {
  param([string]$Path, [object]$Value)
  $temporary = "$Path.tmp"
  $json = $Value | ConvertTo-Json -Compress -Depth 5
  $utf8 = [Text.UTF8Encoding]::new($false)
  try {
    [IO.File]::WriteAllText($temporary, $json, $utf8)
    for ($attempt = 1; $attempt -le 8; $attempt++) {
      try {
        Publish-AtomicFile -TemporaryPath $temporary -TargetPath $Path
        return $true
      }
      catch {
        if ($attempt -eq 8) { return $false }
        Start-Sleep -Milliseconds 10
      }
    }
  }
  finally {
    Remove-Item -LiteralPath $temporary -Force -ErrorAction SilentlyContinue
  }
  return $false
}

function Get-JpegCodec {
  $codec = [Drawing.Imaging.ImageCodecInfo]::GetImageEncoders() |
    Where-Object { $_.MimeType -eq 'image/jpeg' } |
    Select-Object -First 1
  if (-not $codec) { throw 'System.Drawing JPEG codec was not found.' }
  return $codec
}

function Write-NumberFrame {
  param(
    [string]$TemporaryPath,
    [int]$Number,
    [int]$Width,
    [int]$Height,
    [int]$Quality,
    [Drawing.Imaging.ImageCodecInfo]$Codec
  )

  $bitmap = [Drawing.Bitmap]::new($Width, $Height, [Drawing.Imaging.PixelFormat]::Format24bppRgb)
  $graphics = $null
  $font = $null
  $background = $null
  $foreground = $null
  $encoderParameters = $null
  try {
    $graphics = [Drawing.Graphics]::FromImage($bitmap)
    $graphics.Clear([Drawing.Color]::FromArgb(12, 18, 28))
    $graphics.SmoothingMode = [Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $graphics.TextRenderingHint = [Drawing.Text.TextRenderingHint]::AntiAliasGridFit

    $margin = [Math]::Max(12, [Math]::Floor([Math]::Min($Width, $Height) * 0.06))
    $graphics.FillRectangle(
      [Drawing.Brushes]::DarkSlateBlue,
      $margin, $margin, $Width - 2 * $margin, $Height - 2 * $margin)

    $fontSize = [Math]::Max(24, [Math]::Floor([Math]::Min($Width, $Height) * 0.34))
    $font = [Drawing.Font]::new('Arial', $fontSize, [Drawing.FontStyle]::Bold, [Drawing.GraphicsUnit]::Pixel)
    $background = [Drawing.Brushes]::White
    $foreground = [Drawing.Brushes]::Black
    $text = [string]$Number
    $measured = $graphics.MeasureString($text, $font)
    $x = ($Width - $measured.Width) / 2
    $y = ($Height - $measured.Height) / 2
    $graphics.DrawString($text, $font, $background, $x + 4, $y + 4)
    $graphics.DrawString($text, $font, $foreground, $x, $y)

    $encoderParameters = [Drawing.Imaging.EncoderParameters]::new(1)
    $encoderParameters.Param[0] = [Drawing.Imaging.EncoderParameter]::new(
      [Drawing.Imaging.Encoder]::Quality, [int64]$Quality)
    $bitmap.Save($TemporaryPath, $Codec, $encoderParameters)
  }
  finally {
    if ($encoderParameters) { $encoderParameters.Dispose() }
    if ($font) { $font.Dispose() }
    if ($graphics) { $graphics.Dispose() }
    if ($bitmap) { $bitmap.Dispose() }
  }
}

Assert-Administrator
if ($FrameWidth -lt 64 -or $FrameHeight -lt 64) { throw 'Frame size must be at least 64x64.' }
if ($FrameIntervalMs -lt 1) { throw 'FrameIntervalMs must be positive.' }
if ($JpegQuality -lt 1 -or $JpegQuality -gt 100) { throw 'JpegQuality must be in range 1..100.' }
if ($Cycles -lt 0) { throw 'Cycles cannot be negative.' }
if ([IO.Path]::GetFileName($FrameName) -ne $FrameName -or [IO.Path]::GetExtension($FrameName).ToLowerInvariant() -ne '.jpg') {
  throw 'FrameName must be a simple JPG file name without a directory.'
}
if ([IO.Path]::GetFileName($MetadataName) -ne $MetadataName -or [IO.Path]::GetExtension($MetadataName).ToLowerInvariant() -ne '.json') {
  throw 'MetadataName must be a simple JSON file name without a directory.'
}
if (-not $VfsFramePath.StartsWith('/temp/', [StringComparison]::OrdinalIgnoreCase)) {
  throw 'VfsFramePath must be inside the BeamNG temp VFS directory.'
}
$vfsRelativePath = $VfsFramePath.Substring('/temp/'.Length).Replace('/', '\')
if ([IO.Path]::GetFileName($vfsRelativePath) -ne $FrameName) {
  throw 'The VFS frame file name must match FrameName.'
}

Add-Type -AssemblyName System.Drawing
$configFullPath = [IO.Path]::GetFullPath($ConfigPath)
$configDirectory = [IO.Path]::GetDirectoryName($configFullPath)
if (-not [IO.Directory]::Exists($configDirectory)) {
  throw "Config directory does not exist: $configDirectory"
}

$imdisk = (Get-Command imdisk.exe -ErrorAction Stop).Source
$usedLetters = @(Get-PSDrive -PSProvider FileSystem | ForEach-Object { $_.Name.ToUpperInvariant() })
$letter = [char[]]'ZYXWVUTSRQPONMLKJIHGFEDCBA' |
  Where-Object { $usedLetters -notcontains $_ } |
  Select-Object -First 1
if (-not $letter) { throw 'No free drive letter was found.' }
$mount = "$letter`:\"
$attached = $false
$target = $null
$metadataTarget = $null
$junctionPath = $null
$configState = $null
$codec = Get-JpegCodec

try {
  $size = $RamDiskSizeMB.ToString() + 'M'
  & $imdisk -a -t vm -s $size -m $mount -p '/fs:ntfs /q /y' 2>&1 | Out-Host
  if ($LASTEXITCODE -ne 0) { throw "ImDisk attach failed, exit=$LASTEXITCODE" }
  $attached = $true
  Start-Sleep -Milliseconds 700

  $target = Join-Path $mount $FrameName
  $metadataTarget = Join-Path $mount $MetadataName
  $vfsRelativeDirectory = Split-Path $vfsRelativePath -Parent
  if ([string]::IsNullOrWhiteSpace($vfsRelativeDirectory)) {
    throw 'VfsFramePath must contain a temporary subdirectory.'
  }
  $junctionPath = Join-Path $configDirectory $vfsRelativeDirectory
  if ([IO.Directory]::Exists($junctionPath) -or [IO.File]::Exists($junctionPath)) {
    throw "VFS junction path already exists: $junctionPath"
  }
  $junctionParent = Split-Path $junctionPath -Parent
  if (-not [IO.Directory]::Exists($junctionParent)) {
    New-Item -ItemType Directory -Path $junctionParent -Force | Out-Null
  }
  New-Item -ItemType Junction -Path $junctionPath -Target $mount | Out-Null
  $vfsMetadataPath = '/temp/' + ($vfsRelativeDirectory.Replace('\', '/') + '/' + $MetadataName)

  $configState = [ordered]@{
    status = 'running'
    framePath = $target
    vfsFramePath = $VfsFramePath
    metadataPath = $metadataTarget
    vfsMetadataPath = $vfsMetadataPath
    metadataName = $MetadataName
    frameName = $FrameName
    frameWidth = $FrameWidth
    frameHeight = $FrameHeight
    frameNumber = 1
    sequence = 1
    mountPoint = $mount
    producer = 'synthetic_imdisk_frames.ps1'
  }

  $initialTemporary = "$target.tmp"
  Write-NumberFrame -TemporaryPath $initialTemporary -Number 1 -Width $FrameWidth -Height $FrameHeight -Quality $JpegQuality -Codec $codec
  Publish-AtomicFile -TemporaryPath $initialTemporary -TargetPath $target
  $metadataState = [ordered]@{
    status = 'running'
    frameNumber = 1
    sequence = 1
    framePath = $VfsFramePath
    producer = 'synthetic_imdisk_frames.ps1'
  }
  if (-not (Write-JsonFrameMetadata -Path $metadataTarget -Value $metadataState)) {
    throw 'Could not publish the initial frame metadata.'
  }
  if (-not (Write-JsonAtomically -Path $configFullPath -Value $configState)) {
    throw 'Could not publish the initial frame configuration.'
  }
  Write-Host "RAM disk: $mount"
  Write-Host "Frame path: $target"
  Write-Host "Config: $configFullPath"
  Write-Host 'Stop with Ctrl+C. Frames cycle from 1 to 60.'

  $cycle = 0
  while ($Cycles -eq 0 -or $cycle -lt $Cycles) {
    $firstNumber = if ($cycle -eq 0) { 2 } else { 1 }
    for ($number = $firstNumber; $number -le 60; $number++) {
      $temporary = "$target.tmp"
      Write-NumberFrame -TemporaryPath $temporary -Number $number -Width $FrameWidth -Height $FrameHeight -Quality $JpegQuality -Codec $codec
      Publish-AtomicFile -TemporaryPath $temporary -TargetPath $target
      $metadataState.frameNumber = $number
      $metadataState.sequence = ($cycle * 60) + $number
      [void](Write-JsonFrameMetadata -Path $metadataTarget -Value $metadataState)
      Start-Sleep -Milliseconds $FrameIntervalMs
    }
    $cycle++
  }
}
finally {
  if ($configState) {
    $configState.status = 'stopped'
    try {
      [void](Write-JsonAtomically -Path $configFullPath -Value $configState)
    }
    catch {
      # BeamNG may briefly hold the handshake while the producer is stopping.
      # The RAM frame and junction are still cleaned up below.
    }
  }
  if ($attached) {
    & $imdisk -D -m $mount 2>&1 | Out-Host
  }
  if ($junctionPath -and (Test-Path -LiteralPath $junctionPath)) {
    Remove-Item -LiteralPath $junctionPath -Force -ErrorAction SilentlyContinue
  }
  Write-Host "Detached RAM disk: $mount"
}
