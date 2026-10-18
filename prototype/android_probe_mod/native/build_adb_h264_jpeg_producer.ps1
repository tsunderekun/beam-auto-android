param(
  [string]$MsysRoot = 'C:\msys64',
  [string]$OutputDirectory = (Join-Path $PSScriptRoot 'build')
)

$ErrorActionPreference = 'Stop'
$ucrtBin = Join-Path $MsysRoot 'ucrt64\bin'
$gcc = Join-Path $ucrtBin 'g++.exe'
if (-not [IO.File]::Exists($gcc)) { throw "MinGW g++ not found: $gcc" }

New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$source = Join-Path $PSScriptRoot 'adb_h264_jpeg_producer.cpp'
$output = Join-Path $OutputDirectory 'adb_h264_jpeg_producer.exe'
$guiSource = Join-Path $PSScriptRoot 'adb_h264_media_gui.cpp'
$guiOutput = Join-Path $OutputDirectory 'beam_auto_android_media_gui.exe'
$guiManifest = Join-Path $PSScriptRoot 'beam_auto_android_media_gui.manifest'
$guiRc = Join-Path $PSScriptRoot 'beam_auto_android_media_gui.rc'
$guiResource = Join-Path $OutputDirectory 'beam_auto_android_media_gui_manifest.o'
$bash = Join-Path $MsysRoot 'usr\bin\bash.exe'

$msysSource = (& $bash -lc "cygpath -u '$source'").Trim()
$msysOutput = (& $bash -lc "cygpath -u '$output'").Trim()
$command = "export PATH=/ucrt64/bin:/usr/bin:`$PATH; g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -municode -DUNICODE -D_UNICODE `"$msysSource`" -o `"$msysOutput`" `$(`pkg-config --cflags --libs libavcodec libavutil libswscale libjpeg`)"
& $bash -lc $command
if ($LASTEXITCODE -ne 0) { throw "g++ failed with exit code $LASTEXITCODE" }
Write-Host "BUILT=$output"

$msysGuiSource = (& $bash -lc "cygpath -u '$guiSource'").Trim()
$msysGuiOutput = (& $bash -lc "cygpath -u '$guiOutput'").Trim()
$msysGuiRc = (& $bash -lc "cygpath -u '$guiRc'").Trim()
$msysGuiResource = (& $bash -lc "cygpath -u '$guiResource'").Trim()
$guiCommand = "export PATH=/ucrt64/bin:/usr/bin:`$PATH; windres `"$msysGuiRc`" -o `"$msysGuiResource`"; g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -municode -mwindows -static -static-libgcc -static-libstdc++ `"$msysGuiSource`" `"$msysGuiResource`" -o `"$msysGuiOutput`" -lcomdlg32 -lshell32 -lole32 -luuid -lws2_32"
& $bash -lc $guiCommand
if ($LASTEXITCODE -ne 0) { throw "GUI g++ failed with exit code $LASTEXITCODE" }
Write-Host "BUILT=$guiOutput"
