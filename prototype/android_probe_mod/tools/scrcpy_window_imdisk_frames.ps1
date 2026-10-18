param(
  [Parameter(Mandatory = $true)]
  [string]$ConfigPath,
  [Parameter(Mandatory = $true)]
  [string]$VfsFramePath,
  [Parameter(Mandatory = $true)]
  [string]$ScrcpyPath,
  [int]$MaxSize = 720,
  [int]$CaptureIntervalMs = 33,
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
    throw 'Run this scrcpy producer from an elevated PowerShell.'
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
  $json = $Value | ConvertTo-Json -Compress -Depth 6
  $utf8 = [Text.UTF8Encoding]::new($false)
  [IO.File]::WriteAllText($temporary, $json, $utf8)
  if ([IO.File]::Exists($Path)) { Remove-Item -LiteralPath $Path -Force }
  [IO.File]::Move($temporary, $Path)
  return $true
}

function Write-JsonFrameMetadata {
  param([string]$Path, [object]$Value)
  $temporary = "$Path.tmp"
  $json = $Value | ConvertTo-Json -Compress -Depth 6
  $utf8 = [Text.UTF8Encoding]::new($false)
  try {
    [IO.File]::WriteAllText($temporary, $json, $utf8)
    for ($attempt = 1; $attempt -le 8; $attempt++) {
      try {
        Publish-AtomicFile -TemporaryPath $temporary -TargetPath $Path
        return $true
      } catch {
        if ($attempt -eq 8) { return $false }
        Start-Sleep -Milliseconds 10
      }
    }
  } finally {
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

function Save-CapturedBitmap {
  param(
    [IntPtr]$Window,
    [string]$TemporaryPath,
    [int]$Quality,
    [Drawing.Imaging.ImageCodecInfo]$Codec
  )

  $rect = New-Object BeamAutoScrcpyCapture+RECT
  if (-not [BeamAutoScrcpyCapture]::GetClientRect($Window, [ref]$rect)) {
    throw 'GetClientRect failed.'
  }
  $width = $rect.Right - $rect.Left
  $height = $rect.Bottom - $rect.Top
  if ($width -lt 64 -or $height -lt 64) {
    throw "scrcpy client area is too small: ${width}x${height}"
  }

  $origin = New-Object BeamAutoScrcpyCapture+POINT
  $origin.X = 0
  $origin.Y = 0
  if (-not [BeamAutoScrcpyCapture]::ClientToScreen($Window, [ref]$origin)) {
    throw 'ClientToScreen failed.'
  }

  $bitmap = [Drawing.Bitmap]::new($width, $height, [Drawing.Imaging.PixelFormat]::Format24bppRgb)
  $graphics = $null
  $encoderParameters = $null
  try {
    $graphics = [Drawing.Graphics]::FromImage($bitmap)
    $graphics.CopyFromScreen(
      $origin.X, $origin.Y, 0, 0,
      [Drawing.Size]::new($width, $height),
      [Drawing.CopyPixelOperation]::SourceCopy)
    $encoderParameters = [Drawing.Imaging.EncoderParameters]::new(1)
    $encoderParameters.Param[0] = [Drawing.Imaging.EncoderParameter]::new(
      [Drawing.Imaging.Encoder]::Quality, [int64]$Quality)
    $bitmap.Save($TemporaryPath, $Codec, $encoderParameters)
    return [ordered]@{width = $width; height = $height}
  } finally {
    if ($encoderParameters) { $encoderParameters.Dispose() }
    if ($graphics) { $graphics.Dispose() }
    if ($bitmap) { $bitmap.Dispose() }
  }
}

Assert-Administrator
if ($MaxSize -lt 64) { throw 'MaxSize must be at least 64.' }
if ($CaptureIntervalMs -lt 1) { throw 'CaptureIntervalMs must be positive.' }
if ($JpegQuality -lt 1 -or $JpegQuality -gt 100) { throw 'JpegQuality must be in range 1..100.' }
if ($Cycles -lt 0) { throw 'Cycles cannot be negative.' }
if (-not [IO.File]::Exists($ScrcpyPath)) { throw "scrcpy was not found: $ScrcpyPath" }
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
Add-Type @'
using System;
using System.Runtime.InteropServices;

public static class BeamAutoScrcpyCapture
{
    [StructLayout(LayoutKind.Sequential)]
    public struct RECT { public int Left, Top, Right, Bottom; }

    [StructLayout(LayoutKind.Sequential)]
    public struct POINT { public int X, Y; }

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    public static extern IntPtr FindWindow(string className, string windowName);

    [DllImport("user32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool GetClientRect(IntPtr window, out RECT rect);

    [DllImport("user32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool ClientToScreen(IntPtr window, ref POINT point);

    [DllImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool IsWindow(IntPtr window);

    [DllImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool IsWindowVisible(IntPtr window);

    [DllImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool SetProcessDPIAware();
}
'@
[void][BeamAutoScrcpyCapture]::SetProcessDPIAware()

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
$scrcpyProcess = $null
$target = $null
$metadataTarget = $null
$junctionPath = $null
$configState = $null
$codec = Get-JpegCodec
$logDirectory = Join-Path $env:TEMP ('beam_auto_scrcpy_capture_' + [guid]::NewGuid().ToString('N'))
$scrcpyStdout = Join-Path $logDirectory 'scrcpy.stdout.log'
$scrcpyStderr = Join-Path $logDirectory 'scrcpy.stderr.log'
$windowTitle = 'beam_auto_scrcpy_capture_' + [guid]::NewGuid().ToString('N')

try {
  New-Item -ItemType Directory -Path $logDirectory -Force | Out-Null
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

  $scrcpyArguments = @('--no-audio', '--max-size', [string]$MaxSize, '--window-title', $windowTitle)
  $scrcpyProcess = Start-Process -FilePath $ScrcpyPath -ArgumentList $scrcpyArguments `
    -WorkingDirectory ([IO.Path]::GetDirectoryName($ScrcpyPath)) `
    -RedirectStandardOutput $scrcpyStdout -RedirectStandardError $scrcpyStderr -PassThru

  $window = [IntPtr]::Zero
  for ($attempt = 1; $attempt -le 150; $attempt++) {
    $scrcpyProcess.Refresh()
    $window = $scrcpyProcess.MainWindowHandle
    if ($window -eq [IntPtr]::Zero) {
      $window = [BeamAutoScrcpyCapture]::FindWindow($null, $windowTitle)
    }
    if ($window -ne [IntPtr]::Zero -and [BeamAutoScrcpyCapture]::IsWindowVisible($window)) { break }
    if (-not (Get-Process -Id $scrcpyProcess.Id -ErrorAction SilentlyContinue) -or $attempt -eq 150) {
      throw 'Timed out waiting for scrcpy window.'
    }
    Start-Sleep -Milliseconds 200
  }

  $initialTemporary = "$target.tmp"
  $initialSize = Save-CapturedBitmap -Window $window -TemporaryPath $initialTemporary `
    -Quality $JpegQuality -Codec $codec
  Publish-AtomicFile -TemporaryPath $initialTemporary -TargetPath $target
  $frameWidth = $initialSize.width
  $frameHeight = $initialSize.height

  $configState = [ordered]@{
    status = 'running'
    framePath = $target
    vfsFramePath = $VfsFramePath
    metadataPath = $metadataTarget
    vfsMetadataPath = $vfsMetadataPath
    metadataName = $MetadataName
    frameName = $FrameName
    frameWidth = $frameWidth
    frameHeight = $frameHeight
    frameNumber = 1
    sequence = 1
    mountPoint = $mount
    producer = 'scrcpy_window_imdisk_frames.ps1'
    source = 'scrcpy-window-client-area'
    scrcpyPath = [IO.Path]::GetFullPath($ScrcpyPath)
    deviceSelection = 'scrcpy-default-adb-selection'
  }
  $metadataState = [ordered]@{
    status = 'running'
    frameNumber = 1
    sequence = 1
    framePath = $VfsFramePath
    producer = 'scrcpy_window_imdisk_frames.ps1'
    source = 'scrcpy-window-client-area'
    width = $frameWidth
    height = $frameHeight
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
  Write-Host "scrcpy window: $windowTitle"
  Write-Host 'Stop with Ctrl+C. Frames are captured from the live scrcpy window.'

  $sequence = 1
  $number = 1
  $cycle = 0
  while ($Cycles -eq 0 -or $cycle -lt $Cycles) {
    $temporary = "$target.tmp"
    $sizeNow = Save-CapturedBitmap -Window $window -TemporaryPath $temporary `
      -Quality $JpegQuality -Codec $codec
    Publish-AtomicFile -TemporaryPath $temporary -TargetPath $target
    $sequence++
    $metadataState.frameNumber = $number
    $metadataState.sequence = $sequence
    $metadataState.width = $sizeNow.width
    $metadataState.height = $sizeNow.height
    [void](Write-JsonFrameMetadata -Path $metadataTarget -Value $metadataState)
    Start-Sleep -Milliseconds $CaptureIntervalMs
    $number++
    if ($Cycles -gt 0 -and $number -gt 60) {
      $number = 1
      $cycle++
    }
  }
}
finally {
  if ($configState) {
    $configState.status = 'stopped'
    try { [void](Write-JsonAtomically -Path $configFullPath -Value $configState) } catch { }
  }
  if ($scrcpyProcess) {
    $live = Get-Process -Id $scrcpyProcess.Id -ErrorAction SilentlyContinue
    if ($live) {
      Stop-Process -Id $scrcpyProcess.Id -Force -ErrorAction SilentlyContinue
      $scrcpyProcess.WaitForExit(3000)
    }
  }
  if ($attached) { & $imdisk -D -m $mount 2>&1 | Out-Host }
  if ($junctionPath -and (Test-Path -LiteralPath $junctionPath)) {
    Remove-Item -LiteralPath $junctionPath -Force -ErrorAction SilentlyContinue
  }
  Remove-Item -LiteralPath $logDirectory -Force -Recurse -ErrorAction SilentlyContinue
  Write-Host "Detached RAM disk: $mount"
}
