param(
  [Parameter(Mandatory = $true)]
  [string]$ConfigPath,
  [Parameter(Mandatory = $true)]
  [string]$VfsFramePath,
  [Parameter(Mandatory = $true)]
  [string]$AdbPath,
  [Parameter(Mandatory = $true)]
  [string]$ProducerPath,
  [string]$Serial = '',
  [int]$RamDiskSizeMB = 256,
  [int]$JpegQuality = 88,
  [int]$BitRate = 20000000,
  [int]$MaxFrames = 0,
  [ValidateRange(1, 120)]
  [int]$Fps = 60,
  [string]$FrameName = 'android_frame.jpg',
  [string]$MetadataName = 'android_frame_state.json'
)

$ErrorActionPreference = 'Stop'

function Assert-Administrator {
  $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
  $principal = [Security.Principal.WindowsPrincipal]::new($identity)
  if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Run this ADB producer from an elevated PowerShell.'
  }
}

function Get-FreeDriveLetter {
  $used = @(Get-PSDrive -PSProvider FileSystem |
    ForEach-Object { $_.Name.ToUpperInvariant() })
  $letter = [char[]]'ZYXWVUTSRQPONMLKJIHGFEDCBA' |
    Where-Object { $used -notcontains $_ } |
    Select-Object -First 1
  if (-not $letter) { throw 'No free drive letter was found.' }
  return [string]$letter
}

function Publish-TextAtomically {
  param([string]$Path, [string]$Text)
  $temporary = "$Path.tmp"
  $utf8 = [Text.UTF8Encoding]::new($false)
  [IO.File]::WriteAllText($temporary, $Text, $utf8)
  try {
    if ([IO.File]::Exists($Path)) {
      $backup = "$Path.bak"
      Remove-Item -LiteralPath $backup -Force -ErrorAction SilentlyContinue
      [IO.File]::Replace($temporary, $Path, $backup, $true)
      Remove-Item -LiteralPath $backup -Force -ErrorAction SilentlyContinue
    } else {
      [IO.File]::Move($temporary, $Path)
    }
  } finally {
    Remove-Item -LiteralPath $temporary -Force -ErrorAction SilentlyContinue
  }
}

function Set-ConfigStopped {
  param([string]$Path)
  if (-not [IO.File]::Exists($Path)) { return }
  try {
    $config = Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json
    $config.status = 'stopped'
    Publish-TextAtomically -Path $Path -Text ($config | ConvertTo-Json -Compress -Depth 8)
  } catch {
    # A producer may be between two atomic config states during shutdown.
  }
}

Assert-Administrator
if ($RamDiskSizeMB -lt 64) { throw 'RamDiskSizeMB must be at least 64.' }
if ($JpegQuality -lt 1 -or $JpegQuality -gt 100) { throw 'JpegQuality must be 1..100.' }
if ($BitRate -lt 100000) { throw 'BitRate is too small.' }
if ($MaxFrames -lt 0) { throw 'MaxFrames cannot be negative.' }
if (-not [IO.File]::Exists($AdbPath)) { throw "ADB was not found: $AdbPath" }
if (-not [IO.File]::Exists($ProducerPath)) { throw "Producer was not found: $ProducerPath" }
if (-not $VfsFramePath.StartsWith('/temp/', [StringComparison]::OrdinalIgnoreCase)) {
  throw 'VfsFramePath must be inside the BeamNG temp VFS directory.'
}
if ([IO.Path]::GetFileName($FrameName) -ne $FrameName -or
    [IO.Path]::GetExtension($FrameName).ToLowerInvariant() -ne '.jpg') {
  throw 'FrameName must be a simple .jpg file name.'
}
if ([IO.Path]::GetFileName($MetadataName) -ne $MetadataName -or
    [IO.Path]::GetExtension($MetadataName).ToLowerInvariant() -ne '.json') {
  throw 'MetadataName must be a simple .json file name.'
}
$vfsRelativePath = $VfsFramePath.Substring('/temp/'.Length).Replace('/', '\')
if ([IO.Path]::GetFileName($vfsRelativePath) -ne $FrameName) {
  throw 'VfsFramePath file name must match FrameName.'
}
$vfsRelativeDirectory = Split-Path $vfsRelativePath -Parent
if ([string]::IsNullOrWhiteSpace($vfsRelativeDirectory) -or
    $vfsRelativeDirectory -match '(^|[\\/])\.\.([\\/]|$)') {
  throw 'VfsFramePath must contain a safe temporary subdirectory.'
}

$configFullPath = [IO.Path]::GetFullPath($ConfigPath)
$configDirectory = [IO.Path]::GetDirectoryName($configFullPath)
if (-not [IO.Directory]::Exists($configDirectory)) {
  throw "Config directory does not exist: $configDirectory"
}
$junctionPath = Join-Path $configDirectory $vfsRelativeDirectory
$vfsMetadataPath = '/temp/' + ($vfsRelativeDirectory.Replace('\', '/') + '/' + $MetadataName)
if ([IO.Directory]::Exists($junctionPath) -or [IO.File]::Exists($junctionPath)) {
  throw "VFS junction path already exists: $junctionPath"
}

$imdisk = (Get-Command imdisk.exe -ErrorAction Stop).Source
$mount = "$((Get-FreeDriveLetter))`:\"
$attached = $false
$junctionCreated = $false
$producerProcess = $null
$logDirectory = Join-Path $env:TEMP ('beam_auto_adb_h264_' + [guid]::NewGuid().ToString('N'))
$stdoutPath = Join-Path $logDirectory 'producer.stdout.log'
$stderrPath = Join-Path $logDirectory 'producer.stderr.log'

try {
  New-Item -ItemType Directory -Path $logDirectory -Force | Out-Null
  & $imdisk -a -t vm -s ($RamDiskSizeMB.ToString() + 'M') -m $mount `
    -p '/fs:ntfs /q /y' 2>&1 | Out-Host
  if ($LASTEXITCODE -ne 0) { throw "ImDisk attach failed, exit=$LASTEXITCODE" }
  $attached = $true
  Start-Sleep -Milliseconds 700

  $framePath = Join-Path $mount $FrameName
  $metadataPath = Join-Path $mount $MetadataName
  $junctionParent = Split-Path $junctionPath -Parent
  if (-not [IO.Directory]::Exists($junctionParent)) {
    New-Item -ItemType Directory -Path $junctionParent -Force | Out-Null
  }
  New-Item -ItemType Junction -Path $junctionPath -Target $mount | Out-Null
  $junctionCreated = $true

  $msysRoot = if ([string]::IsNullOrWhiteSpace($env:MSYS2_ROOT)) {
    'C:\msys64'
  } else {
    $env:MSYS2_ROOT
  }
  $env:Path = (Join-Path $msysRoot 'ucrt64\bin') + ';' +
    (Join-Path $msysRoot 'usr\bin') + ';' + $env:Path

  $arguments = @(
    '--adb', $AdbPath,
    '--frame-path', $framePath,
    '--metadata-path', $metadataPath,
    '--config-path', $configFullPath,
    '--vfs-frame-path', $VfsFramePath,
    '--vfs-metadata-path', $vfsMetadataPath,
    '--jpeg-quality', [string]$JpegQuality,
    '--fps', [string]$Fps,
    '--bit-rate', [string]$BitRate,
    '--max-frames', [string]$MaxFrames
  )
  if (-not [string]::IsNullOrWhiteSpace($Serial)) {
    $arguments += @('--serial', $Serial)
  }
  $producerProcess = Start-Process -FilePath $ProducerPath -ArgumentList $arguments `
    -WorkingDirectory ([IO.Path]::GetDirectoryName($ProducerPath)) `
    -RedirectStandardOutput $stdoutPath -RedirectStandardError $stderrPath -PassThru

  Write-Host "RAM disk: $mount"
  Write-Host "Junction: $junctionPath -> $mount"
  Write-Host "Config: $configFullPath"
  Write-Host "Producer PID: $($producerProcess.Id)"
  $producerProcess.WaitForExit()
  if ($producerProcess.ExitCode -ne 0) {
    $stderr = if ([IO.File]::Exists($stderrPath)) { Get-Content $stderrPath -Raw } else { '' }
    throw "Native producer failed with exit=$($producerProcess.ExitCode): $stderr"
  }
}
finally {
  if ($producerProcess) {
    $live = Get-Process -Id $producerProcess.Id -ErrorAction SilentlyContinue
    if ($live) {
      Stop-Process -Id $producerProcess.Id -Force -ErrorAction SilentlyContinue
      $producerProcess.WaitForExit(3000)
    }
  }
  Set-ConfigStopped -Path $configFullPath
  if ($junctionCreated -and (Test-Path -LiteralPath $junctionPath)) {
    Remove-Item -LiteralPath $junctionPath -Force -ErrorAction SilentlyContinue
  }
  if ($attached) { & $imdisk -D -m $mount 2>&1 | Out-Host }
  Remove-Item -LiteralPath $logDirectory -Force -Recurse -ErrorAction SilentlyContinue
  Write-Host "Detached RAM disk: $mount"
}
