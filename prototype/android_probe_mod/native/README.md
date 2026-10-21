# Native scrcpy media runtime

> **Проверено на BeamNG 0.40.1.0 / Nothing A059P / Android 16:** scrcpy named-pipe
> video → FFmpeg decode → JPEG в ImDisk/VFS → отображение на vehicle-relative
> ImGui-проекции прошло пользовательскую end-to-end визуальную проверку. GUI
> audio/control bridges запускаются отдельно. Корректность реальной touch-точки
> после исправления координатного диапазона ещё требует проверки. Прямой Android
> `screenrecord --output-format=h264` для телефона завершился с
> `Encoder failed (err=-38)`; USB/ADB и обычный scrcpy работают. Window-capture
> вариант из
> [`../tools/SCRCPY_WINDOW_README.md`](../tools/SCRCPY_WINDOW_README.md) тоже
> **не использовать как видеотранспорт**: он снимал composited desktop и мог
> захватить чужие окна.

Мод создаёт `/temp/beam_auto_android_frame_probe.json` при загрузке Lua extension,
если handshake отсутствует. Это восстановление проверено удалением файла и
перезагрузкой extension.

`adb_h264_jpeg_producer.cpp` — C++ producer для прямого video-тракта.
`adb_h264_media_gui.cpp` — Win32 GUI/launcher, который переносит runtime-логику
PowerShell coordinator-а в C++:

```text
bundled scrcpy.exe --no-video-playback --record <Windows named pipe> --record-format=mkv
  → custom FFmpeg AVIO over named pipe
  → FFmpeg Matroska demux / video decoder
  → libswscale RGB24
  → libjpeg-turbo JPEG
  → atomic `.tmp` + replace
```

Runtime-проверка подтвердила стабильную публикацию JPEG и отображение кадров
телефона в BeamNG. Поток не пишет видеофайл и не захватывает окно/desktop;
`--frame-path` и `--metadata-path` должны указывать на RAM-backed staging,
например ImDisk. Размер и pixel format берутся из `AVFrame`.

## Сборка

Требуется установленный MSYS2 UCRT64 toolchain и пакеты `ffmpeg`/
`libjpeg-turbo`; producer использует `libavformat` дополнительно к
`libavcodec`, `libavutil`, `libswscale` и `libjpeg`:

```powershell
& .\build_adb_h264_jpeg_producer.ps1
```

Результаты:

- `native/build/adb_h264_jpeg_producer.exe` — decoder/publisher worker;
- `native/build/beam_auto_android_media_gui.exe` — GUI launcher.

GUI запускается от имени администратора и позволяет пользователю задать FPS
`1..120`, JPEG quality, размер ImDisk, ADB path, producer path, BeamNG config,
VFS frame path, optional serial и масштаб `0.25`, `0.5`, `0.75` или `1`.
По умолчанию выбран `0.5`. Масштаб применяется перед JPEG encoding.
Кнопка `Rotate` переключает блокировку ориентации Android между `0°` и `90°`
через `adb shell wm user-rotation lock`, используя ADB path и Serial из GUI.
Producer запускает bundled scrcpy для видео без окна и без playback; audio
bridge работает отдельным экземпляром scrcpy. При ошибке демультиплексирования,
декодирования или завершении без кадра producer сообщает ошибку.
Кнопки Start/Stop владеют ImDisk, junction,
native producer и cleanup. В бинарник встроен UAC manifest с
`requireAdministrator`, поэтому Windows сам покажет elevation prompt.
PowerShell coordinator для runtime GUI не нужен.

## Smoke-test без BeamNG

Пути ниже намеренно передаются параметрами. Кадры не должны направляться в
обычный persistent directory:

```powershell
$adb = (Resolve-Path '..\..\..\scrcpy-win64-v5.0.1\adb.exe').Path
$scrcpy = (Resolve-Path '..\..\..\scrcpy-win64-v5.0.1\scrcpy.exe').Path
& .\build\adb_h264_jpeg_producer.exe `
  --adb $adb `
  --scrcpy $scrcpy `
  --serial 00170155H000255 `
  --frame-path 'Z:\android_frame.jpg' `
  --metadata-path 'Z:\android_frame_state.json' `
  --max-frames 60 `
  --fps 60 `
  --scale 0.5 `
  --vfs-frame-path '/temp/beam_auto_android_frame_probe_ram/android_frame.jpg' `
  --vfs-metadata-path '/temp/beam_auto_android_frame_probe_ram/android_frame_state.json'
```

Standalone producer не управляет ImDisk; lifecycle выполняет
`beam_auto_android_media_gui.exe`, создавая ImDisk/junction и передавая handshake
config path.

PowerShell coordinator scripts в `../tools/` сохранены как прежняя диагностика;
основной runtime entry point — `beam_auto_android_media_gui.exe`.

`--scale` принимает только `0.25`, `0.5`, `0.75` или `1`; значение по умолчанию
`0.5`. Producer получает параметры видео из scrcpy Matroska stream и не
фиксирует размер через Android `screenrecord`. Масштабирование выполняется через
`libswscale` перед JPEG. В metadata/config публикуются фактические
`frameWidth`, `frameHeight` и выбранный `scale`.

Размер Android display отдельно считывается через `adb shell wm size`, поэтому
при масштабе `0.5` config содержит display 1080×2392 и JPEG frame 540×1196 на
Nothing A059P. Фактическая точность касания пока проверяется.

`--max-frames 0` означает бесконечную работу, а не частоту кадров. Частота
публикации JPEG задаётся отдельным `--fps 30` или `--fps 60`; по умолчанию
используется `60`. H.264 продолжает приниматься и декодироваться, но JPEG/VFS
обновляется не чаще выбранного лимита.
