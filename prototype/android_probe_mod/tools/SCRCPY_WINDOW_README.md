# scrcpy → ImDisk frame producer

`scrcpy_window_imdisk_frames.ps1` заменяет synthetic producer, не меняя
текущий BeamNG consumer. Он:

1. динамически создаёт ImDisk RAM-диск;
2. создаёт junction внутри фактического BeamNG `temp`;
3. запускает bundled `scrcpy.exe` с динамическим заголовком окна;
4. захватывает client area живого scrcpy-окна через GDI;
5. публикует JPEG атомарно через `.tmp` → replace;
6. публикует metadata с номером кадра, sequence и фактическим размером;
7. при остановке завершает scrcpy, удаляет junction и RAM-диск.

Кадры не записываются на обычный постоянный диск. Временные stdout/stderr
логи scrcpy создаются в `%TEMP%` только как диагностические логи и удаляются
при завершении producer.

## Запуск

PowerShell должен быть elevated. Пути передаются параметрами:

```powershell
$beamngUserPath = Read-Host 'Введите фактический user path BeamNG'
$configPath = Join-Path $beamngUserPath 'temp/beam_auto_android_frame_probe.json'
$scrcpyPath = (Resolve-Path '.\scrcpy-win64-v5.0.1\scrcpy.exe').Path

& .\scrcpy_window_imdisk_frames.ps1 `
  -ConfigPath $configPath `
  -VfsFramePath '/temp/beam_auto_android_frame_probe_ram/android_frame.jpg' `
  -ScrcpyPath $scrcpyPath `
  -MaxSize 720 `
  -CaptureIntervalMs 33
```

`scrcpy` сам выбирает единственное подключённое ADB-устройство. Если устройств
несколько, сначала использовать официальный selector scrcpy или отключить
лишние устройства; serial не зашит в producer.

Текущий вариант намеренно использует window capture, а не private scrcpy API:
это рабочий transport bridge без сборки scrcpy. Прямой `sc_frame_sink` остаётся
будущей оптимизацией и не является условием MVP.
