# Synthetic ImDisk frame proof

`synthetic_imdisk_frames.ps1` — временный диагностический producer. Он не
подключает телефон, ADB или scrcpy. Producer создаёт RAM-диск через найденный в
`PATH` `imdisk.exe`, автоматически выбирает свободную букву, форматирует только
созданный временный том и публикует JPEG-кадры с крупными цифрами `1`–`60`.

Путь к конфигурации и VFS-путь передаются параметрами. Producer создаёт
временную directory junction внутри фактического `temp` BeamNG, которая
указывает на RAM-диск. Поэтому `ImTextureHandler` получает обычный BeamNG VFS
путь, а JPEG остаётся физически в RAM-диске. После остановки junction удаляется.
Текущий номер кадра дополнительно публикуется в `android_frame_state.json`,
также только в RAM-диске; Lua читает этот metadata-файл для диагностической
подписи `FRAME:`.

В скрипте нет имени
пользователя, буквы диска, `D:\BeamNGUSER` или пути установки ImDisk.

Пример запуска из PowerShell **от имени администратора**:

```powershell
$beamngUserPath = Read-Host 'Введите фактический user path BeamNG'
$configPath = Join-Path $beamngUserPath 'temp/beam_auto_android_frame_probe.json'
$vfsPath = '/temp/beam_auto_android_frame_probe_ram/android_frame.jpg'
& .\synthetic_imdisk_frames.ps1 -ConfigPath $configPath -VfsFramePath $vfsPath
```

Параметры `FrameWidth`, `FrameHeight`, `FrameIntervalMs`, `JpegQuality` и
`Cycles` можно передать отдельно. `Cycles=0` означает бесконечный цикл до
`Ctrl+C`. Временные JPEG пишутся только в RAM-диск и заменяются атомарно через
`.tmp`.
