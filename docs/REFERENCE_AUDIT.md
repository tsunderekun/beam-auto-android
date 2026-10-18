# Первичный аудит примеров

Дата: 2026-10-18. Основание: три отчёта GPT Luna 6, сессии в `TEAM.md`.
Статический анализ, без запуска примеров, телефона или игры. Номера строк
ниже относятся к файлам, прочитанным агентами; архив RoadWave не распаковывался.

## Карта компонентов

| Область | RoadWave | backup_adaptive_gestures_v2 | Предложение |
| --- | --- | --- | --- |
| Проекция | `lua/ge/extensions/rw/device.lua`, универсальная ImGui-геометрия | `mod/lua/ge/extensions/android/device.lua`, quad и UV | Сохранить пригодную геометрию и позиционирование |
| Видео Android | Готовый Android-видеоклиент не обнаружен | ADB screenrecord → PyAV → JPEG → RAM/WebSocket и файл | Исследовать scrcpy → декодер → RAM → текстура |
| Звук | `rw/audio.lua`, `rw/stream.lua`: CEF/WebAudio, пространственная обработка | scrcpy → named pipe WAV/PCM → TCP → Lua → WebAudio | Сохранить рабочую игровую часть, проверить совместимость |
| Тач Android | Не установлен как функция RoadWave | UV → жест → HTTP → adb shell input | Сохранить mapping/gestures; согласовать необходимость continuous touch |
| Vivace | Универсальные компоненты | Отдельные JBeam/HTML-элементы | Не включать штатный экран Vivace в целевой мод |

## Конкретные места для будущей работы

Пути без префикса архива ниже относительны `backup_adaptive_gestures_v2/`.

- `streamer/streamer.py:83–120`: поиск BeamNG temp и запись JPEG в
  `android_frame.tmp`/`android_frame.jpg`.
- `streamer/streamer.py:136–170`: поиск ADB/scrcpy; присутствует абсолютный
  путь к пользовательскому Desktop/scrcpy-win64-v4.1.
- `streamer/streamer.py:215–286`: захват через Android screenrecord и декодирование.
- `streamer/streamer.py:322–459`: scrcpy audio, Windows named pipe и TCP bridge.
- `streamer/streamer.py:473–603`: очередь команд, HTTP endpoints, adb input.
- `mod/lua/ge/extensions/android/device.lua:206–385`: геометрия и обратный UV mapping.
- `mod/lua/ge/extensions/android/device.lua:444–511`: координаты и классификация жестов.
- `mod/lua/ge/extensions/android/device.lua:594–615`: файловая ImTextureHandler-текстура.
- `mod/lua/ge/extensions/android/audio.lua:62–168`: WebAudio, PCM, буферизация,
  panner и фильтрация; `188–207`: TCP-подключение.
- `streamer/build.bat:16–17`: путь к Python 3.12. Это часть старого примера,
  не выбор языка нового компонента.
- `streamer/BeamScreenStreamer.spec`: ADB/scrcpy не включены в комплект
  `binaries`/`datas`, поэтому переносимость старого EXE не гарантирована.

## Выводы и границы доказательности

1. В backup кадр уже бывает в RAM, но ImGui читает его из файла. Замена захвата
   сама по себе не устранит файловую зависимость последнего этапа.
2. `AddImageQuad` рисует экранную проекцию, а не материал автомобиля. Нужно
   сохранить ожидаемое пользователем поведение и проверить перекрытия/видимость.
3. Обнаруженный WebAudio-тракт способен описывать пространственный звук;
   переход на нативный аудиомикшер игры не является обязательным требованием.
4. Drag tracking в Lua не означает непрерывную отправку Android down/move/up:
   текущая цепочка отправляет итоговые tap/swipe. По последнему уточнению нужен
   один контакт down/move/up от мыши, без multi-touch. Для непрерывного drag
   потребуется проверить scrcpy control, сохранив пригодный mapping исходника.
5. Отсутствие файлового транспорта не требует zero-copy: допустимость обычной
   загрузки RAM-буфера в GPU оценивается по задержке и нагрузке.
6. Наличие разрешения на DLL не подтверждает существование поддерживаемого
   интерфейса загрузки DLL и регистрации текстур в BeamNG.
7. Сохранять ограниченные буферы, не накапливать устаревшие decoded frames.
   Произвольное отбрасывание сжатых межкадровых пакетов недопустимо без учёта
   зависимостей декодера; политику потерь проектировать на соответствующем этапе.

## Аудит `agenty_universal_jato.zip`

Источник: `D:\BeamNGUSER\current\mods\repo\agenty_universal_jato.zip`.
Архив прочитан и распакован только во временный каталог для анализа; в игру не
устанавливался и не запускался.

### Что подтверждено

- `vehicles/common/AgentY_extraparts/AgentY_jato.jbeam:2–8` объявляет общий
  JBeam-part `AgentY_jato_mainslot`. Его `slotType` включает
  `licenseplate_design_2_1`; комментарий автора прямо объясняет, что этот слот
  выбран как способ сделать part совместимым с большим числом автомобилей.
- Этот part добавляет подслоты для аксессуаров и отдельный
  `AgentY_jato_offset_setup` (`AgentY_jato.jbeam:16–52`). Универсальность
  достигается графом vehicle slots, а не инъекцией произвольного объекта в
  renderer.
- `AgentY_jato_roof.jbeam:34–40` и `AgentY_ramplow.jbeam:8–10` используют
  `flexbodies` для собственной графической геометрии, а nodes/beams — для
  физической привязки к машине. Это подтверждает паттерн **собственный
  физический носитель + mesh, следующий за автомобилем**.
- Архив содержит около 40 `vehicles/*/jato_offset_setup.jbeam`; например,
  `vehicles/vivace/jato_offset_setup.jbeam:12–22` задаёт отдельные offsets.
  `mod_info/MEADFBO8C/info.json:39` предупреждает, что для mod cars может
  потребоваться ручная настройка.

### Что не подтверждено

- JATO не содержит `licenseplate_path`, HTML/WebView, `ImTextureHandler`, ImGui
  или видеопоток. Поэтому `licenseplate_design_2_1` в нём — точка совместимости
  slot graph, а не доказанный динамический texture upload.
- В архиве отсутствуют `.dae` и материалы JATO. По одному архиву нельзя
  проверить mesh, material tags, UV и фактическое отображение в игре.
- Штатный код `lua/common/jbeam/sections/licenseplatesSkins.lua:19–67`
  распознаёт license-plate slots, а
  `lua/ge/extensions/core/vehicles.lua:2212–2231` отдельно запускает штатный
  license-plate renderer. Нельзя автоматически считать, что любой part в этом
  слоте получил доступ к произвольному видеоприёму.

### Следствие для Android-экрана

Рабочая гипотеза теперь формулируется так:

```text
universal JBeam part
  ├─ nodes/beams/flexbody → physical phone frame/screen on the vehicle
  ├─ vehicle material tag → candidate for WebView/canvas or another dynamic source
  └─ GE ImGui without visible picture → mouse state + inverse-project UV → touch
```

Это сохраняет перспективу и не зависит от штатной мультимедиа. Нужно доказать
два независимых участка: (1) part добавляется и следует за произвольной машиной;
(2) его material tag принимает динамический WebView/canvas или другой
подтверждённый кадровый источник. Отрицательная проверка `ImTextureHandler` не
опровергает vehicle-material path, но и не доказывает его.

## scrcpy: ориентиры исследования

- https://github.com/Genymobile/scrcpy/blob/master/doc/develop.md
- https://github.com/Genymobile/scrcpy/blob/master/doc/video.md
- https://github.com/Genymobile/scrcpy/blob/master/doc/audio.md
- https://github.com/Genymobile/scrcpy/blob/master/doc/control.md

Внутренний протокол video/audio/control требует фиксации совместимой версии
server/client. Для следующего этапа ссылки привязать к выбранному release tag.
Не считать stdout штатного CLI готовым embedding API. Выбор между адаптацией
существующего C-клиента и собственным клиентом пока открыт. Аппаратный декодер,
формат метаданных и аудиопараметры проверить по конкретному исходнику версии.

## Материалы для уточнения

Пользователь подтвердил: RoadWave-архив и есть исходник нужного аудиокода,
запрашивать дополнительный мод для этого не нужно.
В метаданных RoadWave упомянут LICENSE.txt, однако агент не обнаружил его
среди entries предоставленного архива. У backup явные условия лицензии
не найдены. Требуется уточнить происхождение аудиосниппетов и материал,
который пользователь предлагал предоставить, особенно перед распространением.
Это наблюдение о комплектности материалов, а не вывод о запрете использования.

## Приоритет

Сначала определить подтверждённый путь RAM → BeamNG texture, затем утвердить
медиамост. Рабочие звук, позиционирование и распознавание жестов по возможности
переиспользовать. Реализацию начинать только по отдельному разрешению.
