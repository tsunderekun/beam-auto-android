# План проекта

Статус: основа архитектуры согласована; разрешён изолированный 3D/WebView
proof-of-concept, полный медиатракт пока не начинался.
Не согласованные явно детали ниже остаются предложениями.

## 1. Уточнить исходные условия
- Целевая установленная версия — BeamNG.drive 0.39.4.0 (сообщена пользователем).
  Исследование `beamng.log:1–2,180–199` подтвердило build 20972 и DirectX 12.
  Vulkan сейчас не выбран.
- Референс — Nothing 3a Pro по USB. Размеры/ориентацию получать динамически,
  не привязывать код к этой модели. Версию Android ещё надо установить.
- Ввод только одной мышью: один контакт down/move/up, нажатие, удержание,
  перетаскивание и свайп. Multi-touch, pinch и сложные жесты не входят в объём.
- Исходный аудиокод уже находится в `roadwave_music_player.zip`; использовать
  его для дальнейшего исследования, без запроса ещё одного архива.
- Согласовать допустимые внешние компоненты и требования к качеству/задержке.

Уже согласовано: EXE и DLL допустимы, сравниваем оба примера, рабочий звук
по возможности сохраняем. Это не означает разрешения реализации.

## 2. Аудит исходных примеров
- Проследить цепочки видео, аудио, touch и 3D-проекции.
- Найти абсолютные пути, файловый транспорт, зависимости и лицензии.
- Выделить пригодные механизмы звука/ввода и зависимости от Vivace.
- Результат: карта компонентов и подтверждённых точек интеграции.

## 3. Проверка реализуемости RAM → текстура (после разрешения)
Это главный технический барьер и первая будущая проверка, до сборки полного мода.
- Установить, какие интерфейсы BeamNG реально доступны в целевой версии.
- Проверить обновляемую текстуру из буфера памяти и её использование текущей
  3D-проекцией. Наличие ImGui.Image само по себе этого не гарантирует.
- Сравнить поддерживаемый интерфейс движка, UI/render-target при наличии
  совместимого пути, либо нативный мост только при согласии пользователя.
- Проверить поток, которому принадлежит загрузка текстуры, освобождение ресурсов,
  смену машины/уровня и совместимость D3D12 текущего стенда; другие backend
  рассматривать только в согласованном объёме.
- Критерий для MVP: движущееся изображение в нужной проекции без записи кадров
  на обычный постоянный диск, через ограниченный RAM-backed staging, bounded
  buffers и измеренную задержку. Прямой native zero-copy не требуется.

Статическое исследование выполнено: `docs/INSTALLED_GAME_RESEARCH.md`.
Прямой RAM upload в Lua не найден; именованные render targets доступны ImGui
по `#name`, но HTML/WebView → такой target/ID не подтверждён. Нативный адаптер
также требует действующего интерфейса, а не только разрешения на DLL.
В EXE найдены строки внутренних `GFXTextureManager`/D3D12 upload и
`TextureDrawPrimitive`, но это не экспортированный ABI. `TextureDrawPrimitive`
рисует синтетические примитивы, а не предоставляет подтверждённый upload
внешнего буфера; вызов внутренних методов по адресам и DLL-инжект исключаются
из безопасного плана без отдельного согласования и доказанной сигнатуры.
Практический WebView-пробник также проверен в BeamNG 0.39.4.0: вызовы
`createUITexture`/`queueJSUITexture` не дали исключения, но через десятки секунд
`ImTextureHandler` для `@`, `#` и обычного имени оставался с размером `0,0` и
форматом `no_format`. Поэтому этот путь пока считать неподтверждённым и не
строить на нём медиатракт.
Предложение первой практической проверки: синтетическое движущееся изображение
→ динамический источник текстуры → отдельный AddImageQuad. Сначала доказать
этот участок без scrcpy/телефона, затем подключать внешний медиапроцесс.
После бинарного прохода это остаётся предложением только при появлении
документированного или runtime-проверяемого texture bridge. Внутренние строки
EXE не дают достаточного контракта; DLL-инжект и вызов методов по адресам в
план не включаются. Это ограничивает только будущий direct/zero-copy путь, а не
выбранный ImDisk RAM-backed MVP.

### 3.1. Дополнительные варианты и оптимизации — не блокеры MVP

1. **Штатный WebView → материал собственного 3D-экрана.**
   `lua/vehicle/htmlTexture.lua` и штатные gauges подтверждают путь WebView
   в vehicle/material. Это может дать визуально настоящий экран на торпедо без
   файлов кадров, но не является `ImGui::AddImageQuad`. Гипотеза требует
   отдельной проверки на синтетическом canvas и независимом custom mesh;
   связь с внешним медиапроцессом и потоковая передача кадров пока не доказаны.
2. **Плоский или перспективный HTML/desktop overlay.**
   Сохраняет RAM-поток и touch, но экран будет привязан к окну/камере, а не к
   геометрии автомобиля. Подходит только при смягчении требования 3D-world
   projection.
3. **Внешний прозрачный Windows overlay с scrcpy-клиентом.**
   Не требует импорта текстуры в BeamNG и технически проще, но ломается при
   движении камеры/изменении окна и не является частью сцены.
4. **In-process D3D12 hook/injector.**
   Теоретически позволяет получить device/queue и внедрить собственный draw,
   но это неподдержанный, версионно хрупкий путь с неизвестным lifecycle и
   риском падения игры. В текущий безопасный план не входит.
5. **`TextureDrawPrimitive` для очень низкого разрешения.**
   Lua-контракт подтверждён для примитивов, но bulk-upload пикселей не найден.
   Видеопоток через отдельные цветные примитивы практически непригоден;
   допустим лишь как диагностический proof-of-concept.
6. **D3D shared memory/shared handle.**
   Может улучшить транспорт между процессами, но сам по себе не даёт BeamNG
   способа импортировать ресурс в `ImTextureID`; без consumer-side bridge это
   не является необходимым для выбранного MVP и остаётся оптимизацией.

7. **Универсальный JBeam-part по паттерну JATO + отдельный vehicle material.**
   Архив `agenty_universal_jato.zip` подтверждает практический способ добавить
   собственную геометрию к автомобилям через общий part, slot graph,
   nodes/beams и `flexbodies`. Для телефона это позволяет сделать физическую
   рамку и экран, следующие за машиной, без штатной мультимедиа. Slot
   `licenseplate_design_2_1` в примере используется как точка совместимости,
   но сам по себе не даёт video upload. Требуется отдельно проверить
   `material tag → Vehicle WebView/canvas` на минимальном собственном mesh.

   Важно: JATO содержит многочисленные vehicle-specific offsets и предупреждает
   о ручной настройке для mod cars. «Универсальный» здесь означает общий
   механизм подключения, а не гарантированно правильную посадку телефона во
   всех автомобилях. Для первого объёма понадобится таблица anchor/offset или
   управляемая настройка положения.

Запись кадров на обычный постоянный диск и попытка подставить чужой D3D handle
в opaque `ImTextureID` остаются исключёнными. ImDisk RAM-backed staging принят
для MVP и уже подтверждён runtime; HUD/overlay остаются альтернативами, а
прямой zero-copy/shared GPU resource — будущими оптимизациями.

### 3.2. Рабочая гипотеза после аудита универсального JBeam-part

```text
universal JBeam part
  ├─ nodes/beams/flexbody → physical phone frame/screen on the vehicle
  ├─ vehicle material tag → WebView/canvas candidate for visual pixels
  └─ GE ImGui (transparent/no draw) → mouse state + inverse-project mapping
                                  → one-finger scrcpy control
```

Проверять гипотезу нужно поэтапно и только после разрешения прототипа:
сначала статическая/синтетическая картинка на собственном vehicle part, затем
динамический WebView canvas, затем поток из медиапроцесса. Штатный
license-plate renderer не считать доказанным произвольным видеоприёмником: в
JATO он используется как slot compatibility, а не как источник изображения.

### 3.3. Итог дополнительного аудита

Аудит локальных BeamNG API и upstream scrcpy завершён 2026-10-18. Выводы
разделяются по потребителям:

| Участок | Статус | Основание |
| --- | --- | --- |
| BeamNG RenderView → `#name` → ImGui | Подтверждён для камеры BeamNG | `render/viewDemo.lua`, `renderViews.lua`, `editor/renderViewBuffers.lua` |
| WebView → vehicle material | Штатный кандидат, runtime custom-mesh ещё не подтверждён | `vehicle/htmlTexture.lua`, `genericGauges.lua` |
| WebView → `ImTextureHandler` → `AddImageQuad` | Отрицательно проверен на текущем probe | `beamng.log`, `android_texture_probe` |
| Внешний RAM → ImGui texture | MVP подтверждён через ImDisk RAM + VFS path; direct pointer upload не найден | synthetic runtime proof, Lua surface и установленная игра |
| scrcpy decoded video/audio/control | Runtime подтверждены вне BeamNG | tag `v5.0.1`, smoke-test телефона, `frame_sink.h`, `audio_player`, `controller` |
| scrcpy → BeamNG MVP video | Разрешён через ImDisk RAM-backed staging | текущий synthetic consumer path |

Следствие: WebView → physical material больше не является обязательным обходом.
Рабочим MVP считается уже подтверждённый ImDisk/VFS → ImGui path; WebView и
direct native texture upload остаются отдельными гипотезами/оптимизациями.

### 3.4. Ограничение безопасности JBeam probe

Первый физический carrier probe признан небезопасным: он добавлял узлы массой
`0.7` и жёстко связывал их балками со штатными узлами Vivace. После сообщения
пользователя о повреждении кузова эти `nodes`, `beams` и `flexbodies` удалены из
обоих probe-part. До разработки отдельного безопасного attachment pattern
JBeam probe считается **visual/controller-only**. Не возвращать силовые связи к
штатным узлам без отдельной проверки массы, collision, beam ownership и
поведения при crash/deformation.

Изменение JBeam-состава требует пересоздания машины или перезапуска уровня;
`Ctrl+L` сам по себе не восстанавливает уже повреждённый экземпляр.

### 3.5. Результат повторной проверки

Пользователь подтвердил: после удаления физической секции машина перестала
ломаться, но изображение осталось прежним. Лог показывает выбранный
`beam_auto_android_probe_vivace`, создание WebView и отдельную загрузку
RoadWave-style ImGui carrier, однако текущий JBeam не содержит `flexbodies`.
Следовательно, это доказательство безопасного controller/WebView lifecycle,
но не доказательство вывода WebView на vehicle material. Считать synthetic
WebView → physical material gate **не пройденным**, а физический carrier probe
— отключённым до разработки безопасного attachment pattern.

Не возвращать старые nodes/beams/flexbodies только ради повторения этого теста:
предыдущая версия уже показала недопустимое воздействие на физику Vivace.

### 3.6. Результат bounded-аудита WebView stream/API

Разрешённая итерация чтения локальных API и примеров завершена без телефона,
ADB, scrcpy, сборки, запуска новых компонентов и изменения физического JBeam.

| Кандидат | Наблюдение | Решение |
| --- | --- | --- |
| `queueWebViewJS` | Передаёт строку JS, собранную из JSON | Только data/control |
| `queueWebViewStreamJS` | Тот же JSON-JS контракт с именем потока; найден в gauges/map | Не считать binary video bridge |
| `ImTextureHandler` | Назначает texture ID/path существующему ресурсу | Не является RAM upload |
| `AddImageQuad` | Рисует уже существующий `ImTextureID` | Не является texture consumer |
| BeamNG.tech shared memory | Читает sensor buffers в capture/tech Lua | Не импортирует внешний material/ImGui texture |
| `beamscreenstream` | Canvas `requestAnimationFrame` → WebView material | Не содержит RAM frame input |

Источники: `lua/vehicle/htmlTexture.lua:7–58`,
`lua/vehicle/controller/gauges/genericGauges.lua:79–93`,
`lua/vehicle/controller/gauges/customModules/navigationData.lua:14–24`,
`lua/common/extensions/ui/imgui_custom_luaintf.lua:199–207`,
`lua/common/extensions/ui/imgui_luaintf.lua:2088–2100`,
`lua/ge/extensions/tech/capturePlayer.lua:354–387,450–494`, а также
локальный `beamscreenstream`.

Вывод bounded-аудита: штатный direct consumer для `RAM video → WebView/material`
и pointer-based `RAM video → ImGui texture` не найден. Это не блокирует MVP:
ImDisk RAM-backed filesystem + VFS path уже является выбранным consumer bridge.
`queueWebViewStreamJS` остаётся только каналом управления/метаданных, а direct
upload можно исследовать позже как оптимизацию. scrcpy runtime уже проверен
отдельно и следующий этап — заменить synthetic producer на scrcpy producer.

Следующий и последний безопасный эксперимент — только visual-only attachment
для synthetic WebView → physical material, без nodes/beams/mass и без возврата
опасной схемы Vivace. Если он не даст видимого движущегося Canvas, texture
consumer считать неподтверждённым и вынести пользователю выбор ослабления
ограничений или остановки проекта на ImGui carrier/input prototype.

Исследование `RAM → ImGui texture` остановить, если одновременно сохраняются:

- отсутствие документированного Lua/native texture upload или external resource import;
- отсутствие WebView-to-ImGui bridge;
- повторный runtime probe не получает ненулевой размер, формат и меняющийся
  контрольный кадр.

Внутренние D3D12-символы EXE, DLL-инжект, вызовы адресов из дизассемблирования
и подстановка shared handle в opaque `ImTextureID` не входят в безопасный план.
Запись кадров на обычный постоянный диск также запрещена; ImDisk RAM-backed
staging для MVP разрешён и уже подтверждён runtime.

## 4. Согласовать архитектуру и язык
Согласованная основа: отдельный медиапроцесс C/C++ и Lua-мод. Небольшой
нативный адаптер текстуры допускается при необходимости, но способ интеграции
с движком ещё должен быть подтверждён. Сначала исследовать адаптацию клиента
scrcpy. Конкретные интерфейсы IPC и загрузки текстуры пока открыты.

Логическая схема потоков:

```text
Android / scrcpy-server
  ├─ video → ADB → приём/декодирование → буферы RAM → текстура → 3D-проекция
  ├─ audio → ADB → декодирование → согласованный звуковой тракт
  └─ control ← ADB ← преобразование координат ← ввод на проекции
```

- Не предполагать, что стандартный scrcpy CLI отдаёт все нужные потоки через stdout.
- Зафиксировать совместимую пару клиента и scrcpy-server и правила обновления.
- Приоритет — адаптация существующего клиента scrcpy. К собственному клиенту
  возвращаться, только если исследование выявит основания пересмотреть решение.
- Медиадекодирование и ADB размещать в отдельном C/C++ процессе, Lua использовать
  для проекции, ввода, настроек и интеграции с игровым звуком.
- Предусмотреть ограниченные очереди кадров, временные метки, сброс устаревших
  кадров и контролируемую A/V-синхронизацию.

### Варианты без Python

| Вариант | Достоинства | Недостатки |
| --- | --- | --- |
| C++ + Lua | Прямой доступ к FFmpeg/Windows graphics API; удобен для нативного моста; близок к C-клиенту scrcpy | Сложнее сборка и управление ресурсами; ошибки DLL могут затронуть игру |
| Rust + Lua | Безопаснее владение памятью; удобен для отдельного процесса транспорта | FFmpeg/graphics interop требует FFI и unsafe; сложнее нативная интеграция |
| C# + Lua | Удобные процессы, сокеты и настройки; быстрое развитие внешнего EXE | Для текстуры нужен native interop; важно контролировать аллокации и упаковку runtime |

После сравнения пользователь согласовал C/C++ для медиапроцесса и Lua для
игрового слоя. Таблица выше сохранена как обоснование выбора. Клиент scrcpy
написан на C; сохранять пригодные C-компоненты без перевода на C++ ради стиля.
По сообщению пользователя MinGW уже установлен. До сборки проверить версию,
разрядность и зависимости. Исследование PATH и типовых каталогов не нашло
toolchain — уточнить путь, не устанавливать другую автоматически.
Upstream scrcpy `v5.0.1` рассмотрен как кандидат: C11, FFmpeg, SDL3,
Meson/Ninja. Отдельно согласовать Python только для сборки Meson; runtime
внешнего EXE остаётся C/C++. Совместимость потенциальной DLL с интерфейсами
BeamNG проверять отдельно: наличие MinGW не гарантирует совместимость C++ ABI.
Не начинать с переписывания рабочего аудиотракта.

### 4.1. Найденный локальный scrcpy-комплект

- В корне проекта найден пользовательский комплект
  `scrcpy-win64-v5.0.1/`: `scrcpy.exe`, `scrcpy-server`, bundled `adb.exe`,
  SDL3/FFmpeg/libusb и ADB DLL. Это подходящая исходная точка для read-only
  inventory и последующего согласования client/server, но не доказательство
  runtime-совместимости с телефоном.
- Для будущего теста выбирать bundled `adb.exe` рядом с client/server и не
  смешивать его с `C:/Program Files (x86)/Iriun Webcam/adb.exe` без отдельной
  проверки host-server. Путь к комплекту не зашивать в исходники: использовать
  пользовательский override или путь относительно внешнего launcher/EXE.
- Ключевые SHA-256 текущего комплекта:
  - `scrcpy.exe` —
    `64BCEE493D9243DAB1E84093928014348738E31F3225E67558210E6B6CFB9CD6`;
  - `scrcpy-server` —
    `764EB6F79811D5211FE9DF341120882BA9994C7A61B897D7BF3FB662E53BC536`;
  - bundled `adb.exe` —
    `B4A6B455702684652CCCF7B46258B29E653538904359A58FD4931CF3EF286B3F`.
- Следующий разрешённый шаг — только read-only сверка состава с upstream
  release/tag и статический inventory зависимостей. `adb devices`, запуск
  scrcpy, подключение телефона, сборка и изменение исходных примеров требуют
  отдельного разрешения.
- Host-side smoke после разрешения пользователя пройден: bundled ADB
  `37.0.1-15733141` и `scrcpy 5.0.1` запускаются, но на изолированном порту
  `5038` устройство не обнаружено. Временный daemon остановлен. Старые чужие
  ADB/server и `streamer_webrtc.py`, которые автоматически возобновляли
  `screenrecord`, очищены; итоговые порты `5037/5038` закрыты. Следующий
  runtime-шаг должен использовать только bundled ADB и начинаться с проверки
  USB-устройства после его фактического подключения.
- Runtime gate телефона теперь пройден вне BeamNG: bundled ADB увидел
  авторизованный Nothing A059P на Android 16/API 36; `scrcpy 5.0.1` передал
  server и получил live video через D3D11VA; отдельный audio-only прогон
  записал 5 секунд в ImDisk RAM и штатно завершился; control smoke поднял
  controller и обработал синтетический mouse `DOWN/UP`. Эти результаты
  подтверждают ADB/server/video/audio/control как внешний транспорт. Прямой
  native RAM-buffer → BeamNG texture API не подтверждён, но для MVP это не
  требуется: выбранный ImDisk RAM-backed VFS path уже подтверждён synthetic
  runtime proof. Android UI-проверка конкретного control действия остаётся
  отдельной задачей.
- Следующий этап — направить decoded scrcpy frame в уже выбранный ImDisk/VFS
  consumer, не переделывая ImGui projection. До реализации зафиксировать
  IPC/ownership контракт внешнего процесса: dynamic width/height/rotation,
  pixel format/stride, PTS, sequence, bounded queue, drop policy, audio
  metadata, status/reconnect и reset активного pointer при disconnect.
- Read-only сверка уже подтвердила локальный `scrcpy-server` официальным
  asset release `Genymobile/scrcpy v5.0.1`: upstream digest
  `764eb6f79811d5211fe9df341120882ba9994c7a61b897d7bf3fb662e53bc536`
  совпадает с локальным SHA-256. Внутри server зафиксирована revision
  `dc8a55a24c27ae78ae202b5a3288da681d0f141a`. Полный Windows-архив и bundled
  `scrcpy.exe` ещё нужно сверить по официальному archive digest, если это
  потребуется для поставки.

Цель первого RAM-пути — исключить файлы кадров. Zero-copy и GPU sharing являются
возможными последующими оптимизациями, а не обязательными условиями первого успеха.

## 5. Последовательная реализация
### 5.0. Текущий разрешённый proof-of-concept

- Создан `prototype/android_probe_mod` с собственным COLLADA carrier,
  material tag `@beam_auto_android_probe_screen`, JBeam-part и Vehicle Lua
  controller на основе штатного `htmlTexture.create`.
- HTML canvas генерирует движущийся synthetic test pattern. Это проверяет только
  vehicle material/WebView path, не RAM-upload и не scrcpy.
- Первый part подключается через `licenseplate_design_2_1`, но beams ссылаются
  на узлы Vivace (`rf1r`, `rf1l`, `f5r`, `f5l`), поэтому текущий прототип
  считается Vivace-only carrier test, а не универсальным модом.
- Распакованная копия установлена в
  `D:\BeamNGUSER\current\mods\unpacked\beam_auto_android_probe`; архивная
  версия не создавалась и не добавлялась.
 - Следующая runtime-проверка требует вручную выбрать именно Vivace-адаптер
   `beam_auto_android_probe_vivace` (не общий `beam_auto_android_probe`) в конфигураторе
   Vivace и перезагрузить конфигурацию. Успехом считаются строки controller/WebView
  в `beamng.log` и видимый экран в driver camera. Отсутствие строк до выбора
   детали не является отрицательным результатом.
- После добавления отдельного `/vehicles/vivace/` адаптера журнал показал, что
  активированный снимок мода был упакован до появления этого файла:
  `beamng.log:172431–172459`. Поэтому перед выбором детали нужно повторно
  активировать мод или перезапустить игру; `Ctrl+L` обновляет Lua, но не
  гарантирует пересборку состава JBeam. Проверять, что в новом `packMod`
  присутствует `vehicles/vivace/android_probe_vivace.jbeam`.

### 5.0.1. RoadWave-style ImGui carrier probe

- Добавлен отдельный GE ImGui carrier probe по мотивам RoadWave. Extension
  размещён по штатному пути `lua/ge/extensions/beam/auto/android/probe.lua` и
  загружается именем `beam_auto_android_probe`, чтобы соответствовать
  преобразованию имён в `common/extensions.lua`.
- Probe пока рисует только синтетический красный/зелёный перспективный quad; он
  не использует телефон, ADB, scrcpy, WebView-текстуру или touch.
- Следующий критерий — после `Ctrl+L` увидеть quad и получить в логе
  `RoadWave-style ImGui carrier probe loaded`; при ошибке нужен лог
  `onPreRender failed: ...`.
- Критерий выполнен: пользовательский скриншот показывает синтетический
  красно-зелёный ImGui carrier в driver camera Vivace с перспективой и
  надписями `ANDROID 3D PROBE / IMGUI VEHICLE ANCHOR`. Следующий этап — не
  подключать телефон, а аккуратно проверить inverse mapping/input на этом
  quad либо отдельно решить, как перенести тот же anchor на физический
  JBeam/flexbody. WebView → material и RAM → ImGui texture bridge пока не
  подтверждены.
- Следующая итерация перенесла диагностический экран с боковой позиции водителя
  на среднюю боковую координату node cloud автомобиля, сохранив глубину и высоту
  относительно driver anchor. Одновременно добавлен локальный input proof:
  inverse mapping экранного quad в `u/v` и один контакт мыши `DOWN/MOVE/UP`.
  Состояние отображается на probe, но ещё не отправляется на Android.
- Пользователь сообщил, что этот вариант «вроде работает». Считать synthetic
  anchor/input proof предварительно принятым и перейти к подготовке реального
  control/video/audio тракта только после отдельного согласования подключения
  телефона и ADB/scrcpy.

### 5.1. Полный медиатракт (MVP consumer выбран)
1. Выбран и runtime подтверждён consumer `ImDisk RAM → dynamic junction →
   BeamNG VFS → ImTextureHandler → AddImageQuad`. Заменить synthetic moving
   producer на scrcpy video producer; прямой native zero-copy не требуется.
2. Согласованный звуковой тракт с учётом удачных решений исходного мода.
3. Тач: геометрия/UV, ориентация, поля изображения, один контакт down/move/up
   от мыши. Проверить непрерывное перетаскивание через scrcpy control, корректное
   завершение контакта при выходе за экран, потере фокуса или соединения.
   Сохранить пригодную логику исходного примера; multi-touch не реализовывать.
4. Жизненный цикл: выбор устройства, разрешение ADB, отключение/переподключение,
   смена машины, пауза, выгрузка и повторная загрузка мода.
5. Установка и настройки: обнаружение путей, пользовательский override, пути
    с пробелами/кириллицей, зависимости, ограниченные диагностические логи.

Подтверждён build-toolchain gate: MSYS2 установлен в `C:\msys64`,
`mingw-w64-ucrt-x86_64-toolchain` предоставляет `gcc/g++ 16.2.0` для
`x86_64-w64-mingw32`. Системный `PATH` не изменён. Следующий отдельный шаг —
только после согласования сборки — подготовить C/C++ producer и подключить
декодер/энкодер без Python runtime.

После host-side smoke-test подключение scrcpy producer к выбранному MVP path
разрешено. Сначала заменить только synthetic producer, сохранив текущий
ImGui consumer; BeamNG texture consumer и Vivace-физику не переделывать.

### 5.1.1. Прямая ADB video-альтернатива

- Runtime-проверка 2026-10-19 подтвердила на Nothing A059P:
  `bundled adb.exe exec-out screenrecord --output-format=h264 -` возвращает
  живой Annex-B H.264 прямо через stdout. За 3 секунды получено `59385` bytes;
  первые NAL units содержали SPS/PPS/IDR (`67/68/65`).
- Это устраняет непринятый оконный путь `scrcpy → CopyFromScreen`: кадры не
  зависят от положения окна, перекрытия, DPI и содержимого монитора.
- Встроенная справка `screenrecord` на текущем Android 16 не перечисляет
  `--output-format`, поэтому эту возможность считать проверенной на текущем
  телефоне, но не переносить без runtime probe на другое устройство.
- `/dev/stdout` не использовать: текущий Android вернул `ERROR: couldn't remove
  existing file`/`Aborted`; рабочая форма — `adb exec-out` с `-`.
- Для C/C++ producer требуется bounded reader H.264 Annex-B, decoder, JPEG
  encoder и тот же ImDisk/VFS publisher. Длины/PTS кадров в raw stdout не
  определены; до выбора A/V архитектуры измерить arrival timing и задержку.
- Ограничение: `screenrecord` покрывает только video. Audio и one-contact
  control остаются отдельными ветками; scrcpy уже подтвердил audio/control
  transport и сохраняется кандидатом для них.

### 5.2. Текущий статус после synthetic RAM-frame proof

- Gate `synthetic JPEG 1..60 → ImDisk RAM → dynamic junction → BeamNG VFS →
  ImTextureHandler → AddImageQuad → perspective carrier` пройден runtime.
  Пользователь подтвердил видимые сменяющиеся цифры и отсутствие пропусков.
- Retry metadata и lifecycle synthetic producer уже закрыты; теперь аналогичный
  lifecycle нужно пройти для scrcpy producer: stop/restart, reconnect, удаление
  RAM-диска/junction и повторное создание texture handler после `Ctrl+L`.
- Локальный один-контактный mapping через bounded FIFO уже подтверждён:
  один drag дал `DOWN → MOVE* → UP`, `DROP: 0`. Следующий control gate —
  передать те же события через scrcpy control channel и проверить reset при
  disconnect.
- Gate пройден: пользовательский скриншот показал `TOUCH: IDLE #7`,
  `TOUCHQ: 0/834`, `LAST: UP`, `DROP: 0` и корректные UV-координаты. Локальный
  один-контактный mapping и bounded sink считать подтверждёнными.
- Фактический bundled `scrcpy`/`adb` уже обнаружен и runtime-проверен на
  Nothing A059P; выбранный MVP consumer уже закрыт через ImDisk/VFS, а C/C++
  producer и IPC остаются следующим implementation gate.
- Прямой ADB video smoke-test также пройден: `exec-out screenrecord` дал
  H.264 stdout без записи кадров на диск. Оконный GDI producer после проверки
  пользовательского скриншота отклонён: он передал участок монитора, поэтому
  не считать его реальным video producer и не подключать к BeamNG.
- Native producer собран и runtime-проверен на телефоне: 10 decoded JPEG
  кадров `1080x2392` через ImDisk RAM, cleanup прошёл. Coordinator
  `adb_h264_imdisk_producer.ps1` создаёт динамический ImDisk/junction и
  публикует handshake для текущего Lua consumer.
- `--max-frames 0` зафиксирован как бесконечный режим. Частота публикации
  задаётся отдельным `--fps` (1..120, default 60); тест `--fps 30` с 10 кадрами
  завершился `EXIT=0` и опубликовал `outputFps=30` в metadata.
- Video end-to-end gate принят пользователем: native producer на `60 FPS`
  работал с реальным телефоном, пользователь нажал `Ctrl+L`, и после Lua reload
  экран телефона появился в текущем vehicle-relative ImGui quad. Следующий
  этап — не менять video consumer, а перейти к one-contact scrcpy control,
  audio sink и stop/restart/reconnect измерениям.
- WebView → физический material больше не является ближайшим шагом для
  подтверждённого ImGui consumer; это отдельная гипотеза. Старые физические
  nodes/beams/flexbodies Vivace не возвращать.

Рабочий цикл Lua: устанавливать мод в уже запущенную игру и использовать
`Ctrl+L` для перезагрузки Lua. Это ускоряет итерации проекции, mapping ввода
и настроек. Нативный EXE/DLL, ADB/scrcpy-соединения, GPU-текстуры и audio
ресурсы должны дополнительно проверяться на корректный shutdown/reconnect,
поскольку Lua reload не гарантирует их автоматическое освобождение.

При тестировании выбирать ровно один режим установки мода: упакованный в
`D:\BeamNGUSER\current\mods` **или** распакованный в
`D:\BeamNGUSER\current\mods\unpacked`. Одновременное присутствие обеих версий
в игре запрещено; перед переключением режима неактивную версию убрать.

## 6. Приёмка
- Подтверждённое отсутствие регулярных записей кадров/видео в файловый транспорт.
  Отдельно учитывать чтение ресурсов, диагностические логи и системный paging.
- Ограниченная память при длительной работе; освобождение процессов/ADB-туннелей.
- Измеренные FPS, end-to-end задержка, A/V-рассинхронизация, CPU/GPU и влияние на игру.
- Работоспособность в согласованной матрице автомобилей, Android и графических API.
- Проверки поворота телефона, tap/drag/swipe/long press одной мышью,
  отсутствия «залипших» нажатий при потере фокуса/соединения.
- Переносимая установка на другом пути без редактирования исходников.

Численные пороги и длительность прогона согласовать с пользователем, а не
назначать как уже принятые требования.
