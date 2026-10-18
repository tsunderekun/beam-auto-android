# Native ADB H.264 producer

`adb_h264_jpeg_producer.cpp` — C++ producer для прямого video-тракта.
`adb_h264_media_gui.cpp` — Win32 GUI/launcher, который переносит runtime-логику
PowerShell coordinator-а в C++:

```text
bundled adb.exe exec-out screenrecord --output-format=h264 -
  → bounded stdout reader
  → FFmpeg H.264 parser/decoder
  → libswscale RGB24
  → libjpeg-turbo JPEG
  → atomic `.tmp` + replace
```

Он не использует Python runtime, окно scrcpy или запись кадров на обычный
диск. `--frame-path` и `--metadata-path` должны указывать на RAM-backed
staging, например ImDisk. Размер и pixel format телефона не зашиты: producer
берёт их из `AVFrame`.

## Сборка

Требуется установленный MSYS2 UCRT64 toolchain и пакеты `ffmpeg`/
`libjpeg-turbo`:

```powershell
& .\build_adb_h264_jpeg_producer.ps1
```

Результаты:

- `native/build/adb_h264_jpeg_producer.exe` — decoder/publisher worker;
- `native/build/beam_auto_android_media_gui.exe` — GUI launcher.

GUI запускается от имени администратора и позволяет пользователю задать FPS
`1..120`, JPEG quality, размер ImDisk, ADB path, producer path, BeamNG config,
VFS frame path, optional serial и масштаб `0.25`, `0.5`, `0.75` или `1`.
По умолчанию выбран `0.5`. Масштаб применяется к JPEG output и пропорционально
(по площади) к bitrate; входной `screenrecord` не получает фиксированный
`--size`, чтобы его `AVFrame width/height` могли измениться при повороте.
Кнопка `Rotate` переключает блокировку ориентации Android между `0°` и `90°`
через `adb shell wm user-rotation lock`, используя ADB path и Serial из GUI.
Перед запуском и после остановки producer очищает зависшие удалённые
`screenrecord`, чтобы аппаратный Android-кодек не оставался занят после
предыдущего Stop/restart. Если видеопоток вернул текстовую ошибку или завершился
без кадра, producer сообщает ошибку вместо ложного успешного Stop.
Кнопки Start/Stop владеют ImDisk, junction,
native producer и cleanup. В бинарник встроен UAC manifest с
`requireAdministrator`, поэтому Windows сам покажет elevation prompt.
PowerShell coordinator для runtime GUI не нужен.

## Smoke-test без BeamNG

Пути ниже намеренно передаются параметрами. Кадры не должны направляться в
обычный persistent directory:

```powershell
$adb = (Resolve-Path '..\..\..\scrcpy-win64-v5.0.1\adb.exe').Path
& .\build\adb_h264_jpeg_producer.exe `
  --adb $adb `
  --serial 00170155H000255 `
  --frame-path 'Z:\android_frame.jpg' `
  --metadata-path 'Z:\android_frame_state.json' `
  --max-frames 60 `
  --fps 60 `
  --scale 0.5 `
  --vfs-frame-path '/temp/beam_auto_android_frame_probe_ram/android_frame.jpg' `
  --vfs-metadata-path '/temp/beam_auto_android_frame_probe_ram/android_frame_state.json'
```

Для полноценного BeamNG lifecycle позже добавляется внешний coordinator,
который создаёт ImDisk/junction и передаёт тот же handshake/config path.
Текущая native-первая итерация намеренно не управляет ImDisk и не изменяет
Lua consumer.

Полный coordinator для этого lifecycle находится в
`../tools/adb_h264_imdisk_producer.ps1`. Он передаёт пути и runtime DLL через
окружение, создаёт динамические ImDisk/junction, запускает native producer и
удаляет staging после остановки.

Он сохраняется как fallback-диагностика, но основной путь после сборки —
`beam_auto_android_media_gui.exe`.

`--scale` принимает только `0.25`, `0.5`, `0.75` или `1`; значение по умолчанию
`0.5`. Producer запрашивает физический размер дисплея через `adb shell wm size`
только для metadata/touch-контракта, но не фиксирует размер входного
`screenrecord` через `--size`: это позволяет decoded `AVFrame` менять
`width/height` при повороте Android. Масштабирование выполняется через
`libswscale` перед JPEG. В metadata/config публикуются фактические
`frameWidth`, `frameHeight` и выбранный `scale`.

`--max-frames 0` означает бесконечную работу, а не частоту кадров. Частота
публикации JPEG задаётся отдельным `--fps 30` или `--fps 60`; по умолчанию
используется `60`. H.264 продолжает приниматься и декодироваться, но JPEG/VFS
обновляется не чаще выбранного лимита.
