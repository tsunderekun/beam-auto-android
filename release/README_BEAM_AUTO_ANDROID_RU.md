# Beam Auto Android — установка и запуск

Релиз состоит из двух ZIP-архивов:

- `beam_auto_android_probe_mod.zip` — BeamNG-мод;
- `beam_auto_android_media_runtime.zip` — native GUI, video producer, DLL,
  bundled ADB и scrcpy.

Python для запуска не требуется.

## 1. Что требуется заранее

- Windows x64;
- BeamNG.drive 0.39.4.0 или совместимая версия;
- подключённый Android-телефон по USB;
- разрешённая USB-отладка и подтверждённый RSA-запрос на телефоне;
- установленный ImDisk/ImDisk Toolkit с доступным `imdisk.exe`.

ImDisk нужен для RAM-backed staging кадров. Установочный архив ImDisk включён
в runtime ZIP в `dependencies\ImDisk\ImDiskTk64.zip`, но сам установщик не
запускается автоматически. Это системный драйвер, который нужно установить
отдельно с правами администратора.

Установка ImDisk из поставки:

1. Распакуйте `dependencies\ImDisk\ImDiskTk64.zip` во временную папку.
2. Откройте распакованную папку `ImDiskTk20260615`.
3. Запустите `install.bat` от имени администратора.
4. После установки перезапустите runtime GUI, если он был открыт.

Проверка после установки:

```powershell
Get-Command imdisk.exe
```

Должен находиться `imdisk.exe`. Runtime GUI также сам запрашивает elevation при
запуске через встроенный UAC manifest.

## 2. Установка мода

Распакуйте `beam_auto_android_probe_mod.zip` в каталог пользовательских модов
BeamNG:

```text
D:\BeamNGUSER\current\mods\
```

Буква диска и путь могут быть другими. В ZIP уже лежат `mod_info.json`,
`scripts/` и `lua/` в правильной структуре. Не распаковывайте содержимое внутрь
дополнительной вложенной папки.

В игре мод загружает универсальный vehicle-relative ImGui carrier. Vivace,
JBeam nodes/beams/flexbodies и штатный мультимедиа-экран не требуются.

## 3. Установка runtime

Распакуйте `beam_auto_android_media_runtime.zip` в отдельную папку, например:

```text
D:\BeamAutoAndroidRuntime\
```

Структура должна сохраниться:

```text
beam_auto_android_media_gui.exe
adb_h264_jpeg_producer.exe
avcodec-63.dll
avutil-61.dll
swscale-10.dll
libjpeg-8.dll
libgcc_s_seh-1.dll
libstdc++-6.dll
libwinpthread-1.dll
scrcpy-win64-v5.0.1\
  adb.exe
  scrcpy.exe
  scrcpy-server
  ...
```

Не перемещайте GUI отдельно от папки `scrcpy-win64-v5.0.1`: GUI автоматически
ищет `scrcpy.exe` и bundled ADB относительно своей папки.

## 4. Проверка ADB

В папке runtime выполните:

```powershell
.\scrcpy-win64-v5.0.1\adb.exe devices
```

Телефон должен иметь статус `device`, а не `unauthorized`.

## 5. Запуск

1. Запустите BeamNG.drive и загрузите автомобиль.
2. Запустите `beam_auto_android_media_gui.exe` от имени администратора.
3. В GUI проверьте:
   - `ADB path` — обычно заполняется автоматически;
   - `Producer EXE` — должен указывать на `adb_h264_jpeg_producer.exe`;
   - `BeamNG config` — путь к `temp/beam_auto_android_frame_probe.json`;
   - `Serial` — необязательно, но рекомендуется указать serial телефона;
   - `Scale` — `0.5` по умолчанию;
   - `FPS`, JPEG quality и RAM MB.
4. Нажмите `Start`.
5. Дождитесь статуса `Running session ...`.
6. Если экран не появился сразу, в BeamNG выполните `Ctrl+L` один раз.

Кадры идут через ADB H.264 → native FFmpeg decode → JPEG → ImDisk RAM →
BeamNG VFS → `ImTextureHandler` → vehicle-relative `AddImageQuad`. Кадры не
пишутся в обычный постоянный каталог.

## 6. Rotate

Кнопка `Rotate` переключает блокировку ориентации Android:

- `Rotate 90` — установить поворот `90°`;
- `Rotate 0` — вернуть `0°`.

После изменения ориентации поток может кратковременно переподключиться. Lua
определяет новые размеры кадра и меняет габариты carrier и touch mapping.

Кнопка меняет ориентацию выбранного serial через:

```text
adb shell wm user-rotation lock 1
adb shell wm user-rotation lock 0
```

Чтобы вернуть автоматический поворот Android вручную:

```powershell
.\scrcpy-win64-v5.0.1\adb.exe -s SERIAL shell wm user-rotation free
```

## 7. Остановка и повторный запуск

- Для остановки нажмите `Stop` и дождитесь статуса `Stopped`.
- Для нового запуска снова нажмите `Start`.
- Не закрывайте GUI принудительно во время обычной остановки.

Producer автоматически очищает зависшие удалённые процессы `screenrecord` до и
после сессии. Это освобождает аппаратный Android-кодек после Stop/Start.

Если старый GUI был уже запущен до обновления runtime, его нужно полностью
закрыть и запустить новый EXE: работающий процесс не подхватывает заменённый
бинарник.

## 8. Управление экраном в BeamNG

- Кнопки `Debug` и `Move` появляются при наведении курсора в зоне немного выше
  верхнего края телефона.
- `Move` включает перемещение carrier-а мышью.
- В Move mode колесо меняет размер, `Shift + колесо` меняет глубину.
- Двойной клик по `Move` сбрасывает placement текущего автомобиля.
- Кнопка `Hide` убирает корпус, текстуру и touch-ввод телефона.
- После скрытия наведите курсор в ту же зону и нажмите `Show`, чтобы вернуть
  экран.
- `Debug` показывает диагностические touch/frame/queue-строки только по запросу.

Настройки placement сохраняются отдельно для автомобиля.

## 9. Если Start снова остановился сразу

Проверьте по порядку:

```powershell
.\scrcpy-win64-v5.0.1\adb.exe devices
Get-Command imdisk.exe
```

Затем:

1. убедитесь, что телефон имеет статус `device`;
2. закройте старый GUI и не запускайте два GUI одновременно;
3. нажмите `Stop`, дождитесь очистки и запустите `Start` заново;
4. проверьте, что `Producer EXE` указывает на файл из этой поставки;
5. при необходимости переподключите USB и подтвердите RSA-запрос.

Producer не считает успешным запуск без кадров: текстовая ошибка Android или
завершение до первого кадра отображаются как ошибка, а не как нормальный Stop.

## 10. Файлы поставки

Исходный код, PowerShell coordinator, `.o`-файл и старые диагностические
примеры в runtime ZIP не входят. Для пересборки нужен отдельный исходный
репозиторий проекта и MSYS2 UCRT64 с FFmpeg/libjpeg-turbo.
