# scrcpy → ImDisk frame producer

> **НЕ ЗАПУСКАТЬ КАК ВИДЕОТРАНСПОРТ.** Runtime-проверка 2026-10-22 показала,
> что `Graphics.CopyFromScreen` захватывает composited desktop: при перекрытии
> окна scrcpy в JPEG попадают другие окна и рабочий стол. Рост sequence и статус
> `running` доказывают только работу цикла GDI, а не чистый кадр телефона.
> Использовать для проверки телефона в BeamNG нельзя.

`scrcpy_window_imdisk_frames.ps1` — сохранённый отрицательный прототип. Он:

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

Откройте PowerShell **от имени администратора** в каталоге этого скрипта
(`prototype\android_probe_mod\tools`). Если bundled runtime лежит отдельно,
передайте путь к его `scrcpy.exe` явно, например:

```powershell
$beamngUserPath = Read-Host 'Введите фактический user path BeamNG'
$configPath = Join-Path $beamngUserPath 'temp/beam_auto_android_frame_probe.json'
$scrcpyPath = Join-Path $env:USERPROFILE 'OneDrive\Desktop\beam_auto_android_media_runtime\scrcpy-win64-v5.0.1\scrcpy.exe'
& .\scrcpy_window_imdisk_frames.ps1 `
  -ConfigPath $configPath `
  -VfsFramePath '/temp/beam_auto_android_frame_probe_ram/android_frame.jpg' `
  -ScrcpyPath $scrcpyPath `
  -MaxSize 720 `
  -CaptureIntervalMs 33
```

Оставьте окно scrcpy видимым и не
перекрывайте его: текущая реализация копирует видимую client area окна через
GDI. Остановка `Ctrl+C` выполняет штатную очистку. Успех проверяется по статусу
`running`, растущим `sequence` в metadata и движущемуся изображению в BeamNG;
само наличие окна scrcpy подтверждает только захват телефона, не BeamNG consumer.

`scrcpy` сам выбирает единственное подключённое ADB-устройство. Если устройств
несколько, сначала использовать официальный selector scrcpy или отключить
лишние устройства; serial не зашит в producer.

Текущий вариант намеренно использует window capture, а не private scrcpy API:
это рабочий transport bridge без сборки scrcpy. Прямой `sc_frame_sink` остаётся
будущей оптимизацией и не является условием MVP.
