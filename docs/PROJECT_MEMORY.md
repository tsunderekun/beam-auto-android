# Память проекта

Обновлено: 2026-10-19.

## Цель
Мод BeamNG.drive: показывать экран Android-телефона в 3D ImGui-проекции
на торпедо автомобиля, передавать действия пользователя обратно телефону
через ADB/scrcpy и воспроизводить телефонный звук в игре.

## Статус и разрешения
- Пользователь явно разрешил пробовать реализацию исследовательского прототипа
  после аудита универсального JBeam-part.
- Выбрана команда из трёх исследовательских агентов `posaprosu/gpt-6-luna`.
- Внешний scrcpy video/audio/control уже прошёл отдельный runtime smoke-test на
  Nothing A059P. Следующий implementation gate — заменить synthetic producer
  на scrcpy producer в уже подтверждённом ImDisk/VFS consumer path.
- Согласована основа: отдельный медиапроцесс C/C++ + Lua-мод; ImDisk
  RAM-backed staging принят для MVP. Нативный direct texture adapter — только
  необязательная post-MVP оптимизация.
- Согласовано сначала рассматривать адаптацию существующего клиента scrcpy.
  Конкретный способ подачи текстуры и детали IPC пока не определены.

## Требования пользователя
- Убрать захардкоженные пути.
- Передавать кадры через оперативную память вместо записи изображений на
  обычный постоянный диск; ImDisk RAM-backed staging разрешён для MVP.
- Изучить получение и видео, и аудио из scrcpy.
- В исходном примере, по словам пользователя, хорошо работают звук и тач.
- Звук использует фрагменты другого мода; пользователь может его предоставить.
- Vivace и его штатная мультимедиа-интеграция не входят в проект.
- Иметь долговременную память проекта и задавать вопросы о реализации.
- При наличии альтернатив Python сначала представить варианты, достоинства
  и недостатки; не выбирать Python автоматически.

## Найденные материалы
- `roadwave_music_player.zip` — предоставленный архив.
- `backup_adaptive_gestures_v2/` — дополнительный пример с `mod/`, `streamer/`,
  `INSTALL_INSTRUCTIONS.md` и `vivace_android_screen.zip`.
- Название backup и его состав не доказывают, что это актуальная версия.
- Все три исследовательских отчёта получены. Сводка: `docs/REFERENCE_AUDIT.md`.

## Ответы пользователя
- Пользователь согласовал внешний медиапроцесс и Lua-мод, с небольшим нативным
  адаптером при необходимости, и выбор C/C++ с приоритетом адаптации scrcpy.
- По разрешению пользователя 2026-10-19 установлен MSYS2 через WinGet и пакет
  `mingw-w64-ucrt-x86_64-toolchain`. Подтверждённый путь toolchain:
  `C:\msys64\ucrt64\bin`; `gcc`/`g++` версии `16.2.0`, target
  `x86_64-w64-mingw32`. Также установлены binutils, headers, CRT, make и GDB.
  Системный `PATH` намеренно не изменялся; для сборки нужно явно добавлять
  `C:\msys64\ucrt64\bin` в окружение процесса либо запускать UCRT64 shell.
  Python-пакет появился как зависимость MSYS2/GDB и не выбран для runtime.
- BeamNG запускается в обычном режиме; пользователь описал его как «DX12/11».
  Vulkan не выбран. Исследование существующего `beamng.log` подтвердило
  DirectX 12: выбор API и успешное создание D3D12 device (строки 180–199).
- Отдельный EXE и DLL допустимы. Это разрешение рассматривать архитектуру,
  а не разрешение начать разработку или доказательство наличия plugin API игры.
- Звук как в RoadWave и пространственный звук из автомобиля допустимы.
  Приоритет: не переписывать рабочий код, если его можно сохранить.
- Сравнить оба предоставленных примера; единственный эталон пока не выбран.
- Целевая установленная версия, по сообщению пользователя: BeamNG.drive 0.39.4.0.
- Референсное устройство — Nothing 3a Pro по USB. Параметры телефона нельзя
  хардкодить: размеры, ориентацию и характеристики потока получать при подключении.
- Последнее уточнение пользователя: ориентироваться только на одну мышь.
  Нужен один контакт с down/move/up для нажатия, удержания и перетаскивания;
  multi-touch и сложные жесты не требуются. Это заменяет прежнюю интерпретацию
  ответа «второй вариант» как требования multi-touch.
- Пользователь уточнил: исходники аудиокода уже в `roadwave_music_player.zip`.
  Дополнительный архив для этого не требуется. Лицензионные сведения проверять
  по имеющимся материалам, отдельно от технического происхождения кода.
- Моды можно устанавливать в запущенную игру. Lua можно перезагрузить прямо
  в игре сочетанием `Ctrl+L`. Это разрешённый рабочий процесс для быстрых
  итераций Lua-слоя; он не заменяет отдельные проверки жизненного цикла EXE/DLL,
  текстурных ресурсов, сокетов и дочерних процессов.
- Пользователь разрешил исследовать следующие локальные каталоги:
  - `D:\BeamNGUSER\`
  - `D:\SteamLibrary\steamapps\common\BeamNG.drive`
- Пользователь разрешил использовать интернет для изучения моддинга BeamNG.drive.
- Пользователь разрешил добавлять мод в зависимости от его состояния:
  - `D:\BeamNGUSER\current\mods` — для соответствующего установленного/упакованного состояния;
  - `D:\BeamNGUSER\current\mods\unpacked` — для рабочей распакованной версии.
- Упакованная и распакованная версии одного и того же мода не должны находиться
  в игре одновременно. При смене режима тестирования сначала убрать или вынести
  неактивную версию, затем установить только выбранный вариант. Не допускать
  одновременных конфликтов загрузки, даже если версии имеют одинаковое содержимое.
- Эти разрешения дают доступ к исследованию и установке мода, но сами по себе
  не отменяют правило: реализацию начинать только после отдельного согласования
  архитектуры и явного разрешения на разработку. Перед изменением существующих
  файлов проверять их состояние и не перезаписывать чужие изменения.

## Предварительные результаты статического исследования
- По статическому аудиту архива, RoadWave содержит универсальную ImGui-проекцию
  и пространственное WebAudio (`rw/device.lua`, `rw/audio.lua`, `rw/stream.lua`),
  но готовый Android-видеоклиент в нём не обнаружен. Android-цепочка находится
  в backup. Это уточняет распределение функций между предоставленными примерами.
- По отчёту агента рендеринга, в backup `mod/lua/ge/extensions/android/device.lua`
  уже есть `ImDrawList_AddImageQuad`, позиционирование относительно автомобиля
  и преобразование ввода. Текстура берётся через `ImTextureHandler` из
  `/temp/android_frame.jpg`. RAM-upload API этим кодом не подтверждён.
- По тому же отчёту, backup `streamer/streamer.py` получает видео через Android
  `screenrecord`, декодирует PyAV, перекодирует OpenCV в JPEG и пишет кадры.
  Видео этого примера не использует транспорт scrcpy.
- Аудио backup использует scrcpy и Web Audio через Lua/JS. Наличие такой цепочки
  не доказывает её работоспособность без связанного UI; нативный микшер BeamNG
  не является обязательным новым требованием пользователя.
- Контроль backup реализован через `adb shell input tap/swipe`; переход на
  scrcpy control не должен потерять удачные алгоритмы жестов исходного примера.
- По исследованию scrcpy: video/audio/control можно принимать отдельными
  каналами клиента. Внутренний протокол зависит от версии; нужен согласованный
  комплект server/client. Штатный CLI не следует считать готовым embedding API.
- Утверждения о конкретных кодеках, метаданных и аппаратном декодере должны
  перепроверяться на выбранном release tag, а не переноситься из master.
- Требование пользователя — не писать кадры на обычный постоянный диск, а не
  обязательный zero-copy. Практический MVP-путь через ImDisk RAM и junction
  подтверждён runtime: JPEG в RAM-backed filesystem потребляется текущим
  `ImTextureHandler`/`AddImageQuad`. Прямой native `RAM pointer → BeamNG
  texture` остаётся только необязательной оптимизацией, не блокером MVP.
- Исследование установленной игры: `beamng.log:1–2` подтверждает 0.39.4.0 x64,
  build 20972; строки 180–199 — DirectX 12, NVIDIA GeForce RTX 5070.
- В исследованной Lua-обвязке не найден прямой upload пикселей в ImGui.
  `renderViewBuffers.lua:75–105` использует `ImTextureHandler("#name")` для
  существующих render targets. Связь `htmlTexture`/WebView с таким ID пока
  не найдена. Не считать DLL обязательной или гарантированно реализуемой.
- `htmlTexture` найден в Vehicle Lua; GE `createUITexture` встречается в
  закомментированном примере, что не доказывает действующий API.
- Выполнена live-проверка синтетического WebView-пробника в BeamNG 0.39.4.0:
  `createUITexture` и `queueJSUITexture` возвращают Lua-успех, а
  `ImTextureHandler` возвращает opaque userdata для `@atp_probe`,
  `#atp_probe_hash` и `atp_probe_hash`. Однако после десятков секунд все три
  остаются `ImVec2(0,0)` и `no_format`; пригодный texture ID для
  `AddImageQuad` не получен. Это не подтверждает HTML/WebView → ImGui texture
  bridge в BeamNG 0.39.4.0. Источник: `D:\BeamNGUSER\current\beamng.log`,
  строки 168501–168946; пробник:
  `D:\BeamNGUSER\current\mods\unpacked\android_texture_probe\lua\ge\extensions\android\texture\probe.lua:58–115`.
- В свежем участке журнала после исправления диагностического рендера новых
  `GameEngineLua:Exception` и `queue failed` не обнаружено. Ранние ошибки
  `ImDrawList_AddText` относятся к предыдущей версии пробника.
- После добавления флага `probeDone` ручная Lua-перезагрузка проверила lifecycle:
  последний запуск завершился строкой `probe complete` на строке 170916
  `beamng.log`; после неё новых `create` и `recheck` нет. Предыдущая запись
  `create` на строке 170390 относится к ещё не исправленному повторному запуску
  и не является результатом текущей версии.
- Найденный `Bin64/VideoStream/libbngvs` кодирует исходящие кадры игры;
  это не обнаруженный входящий видеодекодер или texture upload API.
- В строках `BeamNG.drive.x64.exe` обнаружены внутренние имена
  `GFXTextureManager::createTexture`, `GFXd3d12Device::uploadTextureData`,
  `NamedTexTarget`, `TextureDrawPrimitiveRegistry`, `TheoraTextureObject` и
  `registerLuaApisGfx`. Это подтверждает наличие внутренних реализаций, но не
  публичного ABI: релевантные методы не экспортированы, сигнатуры, layout,
  allocator и render-thread lifecycle не установлены. Вызывать их через
  восстановленные адреса или DLL-инжект нельзя считать поддержанным решением.
- `TextureDrawPrimitiveRegistry` реально используется Lua для рисования
  синтетической геометрии (`lua/ge/extensions/util/materialDebugViz.lua:43–115`,
  `lua/ge/extensions/ui/apps/minimap/minimap.lua:464–492`), но в найденном
  контракте нет внешнего RGBA/RAM upload. Внутренняя строка
  `queueBlitTextureFile` указывает на файловый путь, запрещённый для кадров.
- `RenderViewManagerInstance`/`namedTexTargetColor` создают GPU-target из
  камеры BeamNG (`lua/ge/extensions/render/viewDemo.lua:28–69`), не из буфера
  внешнего процесса. `rawLidar.lua` — специальный BeamNG.tech sensor FFI,
  требующий лицензии и отключённого sandbox, не общий texture/plugin bridge.
- Зафиксированы предложения обхода блокера: штатный WebView в material
  собственного 3D-экрана; плоский HTML/desktop overlay; внешний прозрачный
  Windows overlay; неподдержанный in-process D3D12 hook; диагностический
  `TextureDrawPrimitive`; D3D shared handle/shared memory. Только первый
  сохраняет потенциальную связь с 3D-геометрией BeamNG, но это пока гипотеза
  для custom mesh и не подтверждённый `ImGui::AddImageQuad` path. Hook/injector,
  файловые кадры, RAM-диск и подстановка чужого handle в opaque `ImTextureID`
  исключены из безопасного решения.
- Аудирован архив `D:\BeamNGUSER\current\mods\repo\agenty_universal_jato.zip`
  без установки и запуска. Он подтверждает универсальный JBeam-паттерн:
  общий part входит через `licenseplate_design_2_1`, добавляет собственные
  nodes/beams/flexbodies и прикрепляется к автомобилю. В архиве около 40
  vehicle-specific offset setup файлов; для mod cars возможна ручная настройка.
  Это кандидат для собственного 3D-носителя телефона, но не доказательство
  вывода динамического видео.
- В JATO нет HTML/WebView/ImGui/video-кода, а `.dae` и материалы в архиве не
  присутствуют. Поэтому `licenseplate_design_2_1` считать механизмом
  совместимого slot injection, а не готовым способом загрузки Android-кадров.
- Создан локальный исследовательский мод
  `prototype/android_probe_mod` и установлена только его распакованная копия в
  `D:\BeamNGUSER\current\mods\unpacked\beam_auto_android_probe`.
  Мод содержит собственные `android_probe.dae`, materials JSON, JBeam-part,
  Vehicle Lua controller `androidProbeScreen.lua` и синтетический Canvas/WebView
  `screen.html`; исходные примеры и JATO не изменялись.
- Первый carrier намеренно ограничен Vivace: его четырёхточечные beams ссылаются
  на stock nodes `rf1r`, `rf1l`, `f5r`, `f5l` в
  `prototype/android_probe_mod/vehicles/common/android_probe/android_probe.jbeam:43–49`.
  Общий слот `licenseplate_design_2_1` проверяется отдельно от универсальности
  anchor. Это не считается доказательством работы на всех автомобилях.
- Статическая проверка прототипа пройдена: `android_probe.dae` разбирается как
  XML, оба COLLADA triangle-set имеют согласованные индексы; оба JSON-файла
  проходят `ConvertFrom-Json`. Runtime-загрузка в BeamNG пока не подтверждена:
  установленная машина не была перевыбрана через конфигуратор, в свежем журнале
   нет строк `android_probe`.
- После добавления Vivace-адаптера `vehicles/vivace/android_probe_vivace.jbeam`
  проверка журнала установила причину отсутствия детали: активированный BeamNG
  снимок мода был упакован раньше адаптера. В `beamng.log:172431–172438`
  перечислены файлы упаковки, но `android_probe_vivace.jbeam` отсутствует;
  последующее `unpackMod` также извлекло только старый состав
  (`172452–172459`). Поэтому отсутствие детали в списке пока не является
  отрицательным результатом JBeam или `slotType`.
- Следующая runtime-проверка прошла частично: в выбранном дереве Vivace
  `beamng.log:175...` содержит `chosenPartName="beam_auto_android_probe_vivace"`
  в слоте `licenseplate_design_2_1`, а при создании машины 138969 строка
  `175235` подтверждает `WebView created: @beam_auto_android_probe_screen
  512x1024`. Ошибок JBeam/flexbody/material, относящихся к probe, рядом с
  созданием машины не найдено. Это подтверждает загрузку part и WebView, но не
  является визуальной проверкой положения mesh.
- В более свежем запуске, соответствующем последнему скриншоту, конфигуратор
  снова выбрал общий part `beam_auto_android_probe`, а не Vivace-адаптер:
  `D:\BeamNGUSER\current\beamng.log:712–713` содержит
  `chosenPartName="beam_auto_android_probe"` и одновременно показывает
  `beam_auto_android_probe_vivace` в `suitablePartNames`. Строка 758 подтверждает
  создание того же WebView, но имя WebView одинаково у обоих parts и потому не
  различает выбранную деталь. Следовательно, последний скриншот нельзя считать
  визуальной проверкой именно `beam_auto_android_probe_vivace`; перед следующим
  сравнением нужно явно выбрать деталь с именем `beam_auto_android_probe_vivace`
  и перезагрузить конфигурацию.
- Последующая ручная проверка выбора исправила это: свежий запуск содержит
  `chosenPartName="beam_auto_android_probe_vivace"` в
  `D:\BeamNGUSER\current\beamng.log:1189–1190`. На предоставленном скриншоте
  выбран нижний пункт `Android 3D Screen Probe (Vivace test adapter)`, то есть
  правильный Vivace carrier. В driver-camera по-прежнему виден штатный экран с
  картой; синтетический экран пробника и его frame визуально не подтверждены.
  Это отделяет проблему выбора part от следующей проверки видимости mesh/material.
- После явного разрешения пользователя продолжена реализация изолированного
  synthetic carrier test. Статическое сравнение показало, что исходный DAE
  пробника имел geometry около нулевых координат (`y≈0`, `z=-0.19..0.19`), тогда
  как узлы Vivace carrier находятся у `y=-0.31`, `z=0.96..1.34`. Из-за этого
  mesh мог загружаться вне ожидаемой зоны driver camera. В
  `prototype/android_probe_mod/vehicles/common/android_probe/android_probe.dae`
  frame перенесён в `x=±0.105`, `y=-0.33..-0.29`, `z=0.96..1.34`, а screen — в
  `x=±0.078`, `y=-0.289`, `z=0.995..1.305`. XML-проверка прошла; файл установлен
  в `D:\BeamNGUSER\current\mods\unpacked\beam_auto_android_probe` и совпадает с
  исходником по SHA-256 `7D2125138D8F1FF621A5CBF1517AAAA804872FCDDA1C8607E8E427CB28D7149B`.
  Телефон, ADB и scrcpy по-прежнему не подключались.
- После перезагрузки машины BeamNG действительно перечитал DAE: в свежем
  участке `D:\BeamNGUSER\current\beamng.log:1268–1269` есть
  `Shape not imported yet: /vehicles/common/android_probe/android_probe.dae` и
  `Importing COLLADA: android_probe.dae ...`, без ошибки импорта probe. Выбор
  `beam_auto_android_probe_vivace` также сохраняется. Собственный frame/screen в
  driver camera всё ещё не виден, поэтому выполнен отдельный диагностический
  тест material mapping: в `android_probe.materials.json` frame временно сделан
  ярко-красным, screen — ярко-зелёным, а `emissiveMap` WebView временно убран.
  Файл синхронизирован с распакованной копией мода; JSON-проверка прошла.
  Следующий наблюдаемый результат должен разделить проблему flexbody/геометрии
  и проблему WebView → material. Это временный диагностический вариант, не
  финальный материал. Телефон, ADB и scrcpy не подключались.
- По явному запросу пользователя добавлен отдельный RoadWave-style ImGui carrier
  probe, не изменяющий исходный RoadWave: `prototype/android_probe_mod/lua/ge/
  extensions/beam/auto/android/probe.lua`. Extension берёт vehicle-relative basis
  из `cameraRefNodes.driver`/`refNodes`, обновляет anchor по текущим узлам машины,
  проецирует четыре world-точки через `core_camera` и рисует перспективный
  красный корпус/зелёный экран с `GetBackgroundDrawList1`. Загрузка подключена в
  `prototype/android_probe_mod/scripts/beam_auto_android_probe/modScript.lua`;
  оба файла синхронизированы с `D:\BeamNGUSER\current\mods\unpacked\
  beam_auto_android_probe`. Это экранная ImGui-проекция, а не физическая `.dae`
  модель и не доказательство texture bridge. Следующая проверка — `Ctrl+L` и
  driver camera; телефон, ADB, scrcpy и ввод не подключаются.
- В ходе проверки lifecycle исправлено имя GE-extension. Штатный загрузчик
  преобразует имя `beam_auto_android_probe` в путь
  `lua/ge/extensions/beam/auto/android/probe.lua`; прежний плоский файл
  `beamAutoAndroidProbe.lua` удалён из установленной копии. Убрана также
  неверная проверка результата `extensions.load`: в BeamNG 0.39.4 этот вызов не
  возвращает объект. Внешний probe теперь использует `pcall` только вокруг
  per-frame `onPreRender` и пишет одноразовую ошибку при runtime-сбойке.
  Исходный и установленный Lua-код синхронизированы; телефон, ADB, scrcpy и
  touch не подключались. Требуется повторная проверка через `Ctrl+L`.
- Получен пользовательский скриншот после исправления extension path. На нём
  виден красный корпус с зелёным экраном `ANDROID 3D PROBE / IMGUI VEHICLE
  ANCHOR` в driver camera Vivace. Это визуально подтверждает загрузку ImGui
  carrier, перспективную проекцию четырёх точек и vehicle-relative anchoring.
  Скриншот не подтверждает физический DAE/flexbody и WebView/material bridge:
  показанный carrier рисуется только GE ImGui. Надпись `Unknown app:
  rwRadioBrowser` в верхней части кадра относится к постороннему RoadWave/UI
  состоянию, а не к android probe.
- По запросу пользователя anchor изменён с водительской боковой позиции на
  центральную линию салона. В
  `prototype/android_probe_mod/lua/ge/extensions/beam/auto/android/probe.lua`
  `buildAnchor` вычисляет среднюю боковую координату node cloud автомобиля и
  сохраняет от driver anchor только продольную и вертикальную координаты.
  Поэтому экран должен переместиться от руля к центру салона, оставаясь
  vehicle-relative.
- В тот же синтетический probe добавлен диагностический один-контактный input:
  `GetMousePos`, `IsMouseClicked(0)`, `IsMouseDown(0)`, `IsMouseReleased(0)` и
  affine inverse mapping screen quad → `u/v`. Состояния `DOWN`, `MOVE`, `MOVE
  OUT`, `UP`, `HOVER` и номер последовательности рисуются на экране. ADB,
  scrcpy и телефон не подключаются; touch наружу не отправляется.
- Обновлённый Lua-файл синхронизирован с установленной распакованной копией;
  SHA-256 источника и установленного файла совпадает:
  `7A0A22689588C0FE417A4950C79712711AE6F09D2DECAF089D36DB19FF3F1D44`.
- Пользователь сообщил, что центральное позиционирование и диагностический
  один-контактный input «вроде работает». Это принимается как предварительное
  подтверждение synthetic path: ImGui carrier, vehicle-relative center anchor,
  perspective projection и локальные `DOWN/MOVE/UP` с inverse `u/v` работают на
  стенде. Подтверждение пока не распространяется на физический DAE, WebView →
  material, RAM texture, телефон, ADB или scrcpy.
- Текущее положение задаётся `prototype/android_probe_mod/vehicles/vivace/
  android_probe_vivace.jbeam:27–46`: собственные узлы находятся примерно в
  диапазоне `x=±0.105`, `y=-0.31`, `z=0.96..1.34` и связаны с Vivace `rf1r`,
  `rf1l`, `f5r`, `f5l`. По исходным координатам Vivace `rf1r/rf1l` находятся
  у лобового стекла (`z≈1.35`), а `f5r/f5l` — ниже и спереди (`y≈-0.943,
  z≈0.599`). Поэтому текущий carrier следует ожидать в центре передней части
  салона, вертикально, между нижней зоной лобового стекла и передней частью
  кузова; это ещё не доказательство аккуратной посадки на торпедо.
- Зафиксирована гипотеза: JBeam-part обеспечивает физическую перспективную
  геометрию и material tag, а GE ImGui остаётся прозрачным слоем чтения мыши и
  inverse-project UV → touch. В старом `device.lua` mapping уже отделён от
  изображения (`:360–385`, `:444–513`).
- Проверен upstream scrcpy tag `v5.0.1`: `sc_frame_sink` подходит как точка
  адаптации видео/PCM, `sc_controller_push_msg` — для ввода. Tag пока кандидат,
  окончательная фиксация версии и зависимости не согласованы. В develop.md
  этого tag остались примеры протокола 4.0; руководствоваться кодом tag.
- Meson требует Python на этапе сборки; приложение может остаться C/C++ без
  Python runtime. Использование Python для сборки отдельно не согласовано.
- Подробности и первичные источники: `docs/INSTALLED_GAME_RESEARCH.md`.

## Результаты дополнительного аудита 2026-10-18

Аудит выполнен только чтением локальных исходников, бинарных строк, журналов и
upstream scrcpy; реализация, сборка, запуск телефона/ADB и изменение исходных
примеров не проводились.

### BeamNG: источники изображения

- Подтверждён штатный путь `RenderView камеры BeamNG → named texture target
  (#name) → ImTextureHandler → ImGui`. Источники: `lua/ge/extensions/render/
  viewDemo.lua:28–69`, `renderViews.lua:75–107`, `editor/renderViewBuffers.lua:
  75–105`. Он предназначен для камеры/сцены BeamNG и не принимает внешний
  RAM-буфер или WebView surface.
- Подтверждён штатный кандидат `htmlTexture.create() → obj:createWebView() →
  vehicle material`. Источники: `lua/vehicle/htmlTexture.lua:7–59`,
  `lua/vehicle/controller/gauges/genericGauges.lua:180–194,220–224`.
  Синтетический Canvas/WebView допустим как источник этого material path, но
  полная runtime-проверка движущегося изображения на собственном mesh ещё не
  выполнена.
- Отрицательный runtime-результат для гипотезы `WebView/UI texture tag →
  ImTextureHandler → AddImageQuad` сохраняется: в BeamNG 0.39.4.0 варианты
  `@`, `#` и обычного имени оставались `ImVec2(0,0)`/`no_format`.
- В Lua не найден API вида `pointer + width + height + stride + format →
  ImGui texture`. Внутренние имена `GFXTextureManager`, `uploadTextureData`,
  `TheoraTextureObject` и `TextureDrawPrimitive` в EXE не являются доказанным
  ABI; вызовы по восстановленным адресам и DLL-инжект остаются исключёнными.
- Поэтому `AddImageQuad` подтверждён только как перспективный потребитель уже
  существующего texture ID. Подтверждённого штатного `RAM → ImGui texture`
  bridge для внешнего видеопотока пока нет.

### scrcpy и внешний медиапроцесс

- Для upstream tag `v5.0.1` подтверждены внутренние точки адаптации: video
  `sc_frame_sink` получает decoded `AVFrame`, audio использует тот же sink
  abstraction, а control принимает `sc_control_msg` через
  `sc_controller_push_msg`. Источники: `app/src/trait/frame_sink.h`,
  `app/src/audio_player.h/.c`, `app/src/controller.h`, `app/src/control_msg.h`.
- Один контакт можно представить последовательностью `DOWN/MOVE/UP` с одним
  pointer ID. Move допускает coalescing только по отдельной политике; `DOWN` и
  `UP` нельзя безусловно отбрасывать. При потере фокуса/соединения нужен
  локальный reset и закрытие активного контакта.
- Video/audio/control идут отдельными каналами. Client и `scrcpy-server.jar`
  должны быть из одного tag/совместимого комплекта; внутренний протокол не
  является стабильным embedding SDK. `stdout` не принят как общий транспорт.
- До подтверждения texture consumer можно проектировать только транспортные
  интерфейсы, bounded queues и lifecycle. Запись AVFrame в BeamNG texture,
  импорт D3D shared handle и привязка к render thread не подтверждены.
- Runtime внешнего медиапроцесса может оставаться C/C++; Python не требуется.
  Python/Meson допустимы только как возможная build-time зависимость после
  отдельного согласования; сборка не начиналась.

### Аудит исходных примеров

- RoadWave полезен как reference для vehicle-independent 3D-проекции,
  spatial WebAudio, muffling, разделения Lua-extension и lifecycle.
- `backup_adaptive_gestures_v2` полезен как reference для Android audio,
  UV/ориентации и gesture mapping. Его видео регулярно пишет кадры в
  `temp` (`streamer/streamer.py:83–120`), поэтому этот путь не переносится.
- Backup во внешний control отправляет итоговые tap/swipe, а не непрерывные
  Android `DOWN/MOVE/UP` (`mod/lua/ge/extensions/android/device.lua:465–511`).
  Для текущего требования один контакт нужно передавать через scrcpy control,
  сохранив inverse mapping исходного примера.
- Из backup можно сохранить bounded PCM/переподключение и spatial audio как
  основу, но нужно заменить hardcoded paths, Vivace-зависимости и файловый
  frame/info transport. Работоспособность этих цепочек в целевой версии
  статическим наличием кода не доказана.

### Итоговый технический gate

Первоначальный блокер прямого `RAM pointer → BeamNG texture` отменён как
обязательное условие MVP. Выбран и подтверждён runtime путь
`scrcpy video → JPEG → ImDisk RAM → junction → BeamNG VFS → ImTextureHandler
→ AddImageQuad`. Прямой native/shared-GPU upload остаётся необязательной
оптимизацией; WebView → physical material не является обязательной веткой.

### Диагностическая проверка ImDisk 2026-10-18

- По отдельному разрешению пользователя выполнена изолированная проверка
  установленного ImDisk; BeamNG, моды, телефон, ADB и scrcpy не запускались и
  не изменялись.
- Elevated PowerShell нашёл `imdisk.exe` через системный `PATH`; путь установки
  `C:\Program Files\ImDisk` в код не требуется и не использовался как
  обязательный путь.
- Свободная буква выбиралась динамически. Первый вариант с raw-томом без
  параметра форматирования создавал устройство и volume GUID, но Windows не
  публиковала доступную файловую систему. Это диагностический факт, а не
  проблема абсолютного пути.
- Вариант `imdisk -a -t vm -s 64M -m <dynamic-letter> -p "/fs:ntfs /q /y"`
  прошёл: временный RAM-диск стал доступен, запись/чтение тестового файла
  вернули `ok`, затем устройство было удалено в `finally`.
- ImDisk RAM-backed staging теперь согласован как MVP-архитектура и успешно
  доказан через `ImTextureHandler`/`AddImageQuad`. Ограничение остаётся только
  на запись кадров на обычный постоянный диск. Прямой pointer/GPU upload не
  найден, но переведён в необязательную optimization backlog.
- Динамический runtime-контракт каталога кадров, синтетические движущиеся кадры
  и текущий ImGui consumer уже проверены; телефон и scrcpy также прошли внешний
  video/audio/control smoke-test. Следующий шаг — заменить synthetic producer
  на scrcpy producer, не меняя consumer.
- Пользователь явно разрешил этот следующий шаг и выбрал временный
  PowerShell/.NET producer вместо Python или C++. Добавлен
  `prototype/android_probe_mod/tools/synthetic_imdisk_frames.ps1`: он принимает
  путь конфигурации параметром, динамически выбирает букву ImDisk, создаёт
  кадры с цифрами `1`–`60` и публикует конфигурацию с абсолютным runtime-путём
  RAM-кадра. Lua probe читает только относительный VFS-конфиг
  `/temp/beam_auto_android_frame_probe.json`, затем обновляет
  `ImTextureHandler`/`AddImageQuad` и сохраняет существующий perspective/touch
  carrier. Скрипт и Lua пока не запускались в BeamNG после этого изменения.

- Runtime proof завершён на установленном BeamNG 0.39.4.0: на скриншоте
  пользователя в перспективном ImGui carrier виден реальный JPEG с крупной
  цифрой `10`; producer прошёл последовательность цифр `1`–`60` без пропусков
  кадров по сообщению пользователя. Lua показывает `FRAME: 1 PRODUCER RUNNING`,
  а изображение на экране меняется.
- Для доступа texture handler добавлена динамическая directory junction внутри
  фактического BeamNG `temp`: VFS-путь
  `/temp/beam_auto_android_frame_probe_ram/android_frame.jpg` указывает на
  runtime-путь RAM-диска, который producer получил после динамического выбора
  mount point. JPEG не записывается на обычный постоянный диск.
- Подтверждено запуском: RAM-файл меняется по размеру и времени; файл доступен
  через junction; `ImTextureHandler`/`AddImageQuad` отображает JPEG в
  перспективной проекции. Это диагностический proof для согласованного ImDisk
  этапа, не доказательство финального scrcpy/RAM-pointer consumer.
- Для устранения статичной подписи `FRAME: 1` producer теперь публикует
  `android_frame_state.json` на том же RAM-диске через atomic replacement.
  Lua читает его через `/temp/beam_auto_android_frame_probe_ram/` и обновляет
  номер кадра отдельно от стартового JSON handshake. Runtime-проверка показала
  `META_1_FRAME=6/SEQUENCE=126` и через 450 мс
  `META_2_FRAME=10/SEQUENCE=130`, то есть metadata действительно меняется.
- После этого изменения исходный Lua синхронизирован с установленной копией;
  PowerShell producer прошёл parser check и одноцикловый `1..60` test с
  `INNER_EXIT=0`. Live producer снова запущен для проверки подписи после
  `Ctrl+L`.
- Пользователь подтвердил визуально: «есть контакт». Это принимается как
  runtime-подтверждение полного диагностического участка:
  `synthetic JPEG → RAM-диск → junction → BeamNG VFS → ImTextureHandler →
  ImGui AddImageQuad → vehicle-relative perspective projection`; крупные цифры
  кадров меняются, последовательность `1..60` проходит без пропусков.
- Текущее состояние сохранено отдельным timestamped snapshot в каталоге
  `backups/beam_auto_android_probe_20261018_230913/`; snapshot создаётся после
  этого runtime proof и содержит исходник,
  установленную распакованную копию, документы и manifest. Живой RAM-диск и
  временный JPEG в snapshot намеренно не включаются.
- После следующего обновления создан более свежий snapshot
  `backups/beam_auto_android_probe_20261018_232101/`. Проверка manifest прошла:
  `MANIFEST_VERIFY=OK`, 25 файлов; SHA-256 исходного и установленного probe
  совпадает `1BB50D60E4348873AE47329F7EB6671D7EB8BB6FD736CE60519BC313F1DCB7B5`.
- Одна проверка metadata попала в момент atomic replacement и получила Windows
  `IOException`. Это ожидаемая гонка reader/writer; JPEG-путь и подтверждённое
  отсутствие пропусков этим не опровергнуты. Следует добавить короткий retry в
  Lua, чтобы диагностическая подпись не теряла номер кадра.
- Lifecycle-тест producer прошёл с `INNER_EXIT=0`: RAM-диск отключается,
  junction удаляется, stale ImDisk mount point не остаётся. Обнаружен отдельный
  minor defect: после штатного конечного цикла handshake оставался со статусом
  `running`. Producer исправляется публикацией `status=stopped` в `finally` до
 отключения RAM-диска; принудительное завершение всё равно требует внешней
 очистки stale handshake.
- В текущий Lua probe добавлен bounded FIFO touch sink ёмкостью 32 события.
  Он принимает только локальные `DOWN/MOVE/MOVE OUT/UP`, вычитывает максимум
  одно событие за render frame и показывает `TOUCHQ`, общий счётчик событий,
  последнее состояние и `DROP`. Наружу события пока не отправляются; телефон,
  ADB и scrcpy не подключались. Исходный inverse UV mapping и один контакт не
  изменены. Исходник синхронизирован с установленной копией, SHA-256 текущего
  probe: `6D9527153A087688E3529224984258D232D888150BDA18B8C60134646D04E0FC`.
- Пользовательский runtime-скриншот подтвердил один контакт: экран показал
  `TOUCH: IDLE #7`, `UV: 0.852, 0.638`, `TOUCHQ: 0/834`, `LAST: UP`,
  `DROP: 0`. Следовательно, один drag завершился `DOWN → MOVE* → UP`, 834
  события были обработаны bounded sink без переполнения. Ошибки FFB/input
  keyboard в консоли на этом скриншоте относятся к штатному input mapping
  автомобиля, не к android probe.
- Пользователь добавил в корень проекта комплект
  `scrcpy-win64-v5.0.1/`. Read-only inventory подтвердил наличие одной
  переносимой связки: `scrcpy.exe` (766717 bytes), `scrcpy-server`
  (733930 bytes) и соседний `adb.exe` (8273560 bytes), а также SDL3,
  FFmpeg `avcodec-63/avformat-63/avutil-61/swresample-7`, libusb и
  `AdbWinApi/AdbWinUsbApi` DLL. В комплекте есть `LICENSE.txt` с Apache License
  2.0.
- Зафиксированы SHA-256 ключевых компонентов комплекта:
  `scrcpy.exe` —
  `64BCEE493D9243DAB1E84093928014348738E31F3225E67558210E6B6CFB9CD6`,
  `scrcpy-server` —
  `764EB6F79811D5211FE9DF341120882BA9994C7A61B897D7BF3FB662E53BC536`,
  bundled `adb.exe` —
  `B4A6B455702684652CCCF7B46258B29E653538904359A58FD4931CF3EF286B3F`.
  Полный inventory DLL и файлов получен read-only; отдельный официальный
  upstream checksum ещё не сверялся.
- Bundled ADB из `scrcpy-win64-v5.0.1/` считать первым кандидатом для будущей
  пары client/server. Найденный ранее
  `C:/Program Files (x86)/Iriun Webcam/adb.exe` (ADB 1.0.41,
  29.0.1-5644136) не смешивать автоматически с комплектом scrcpy; его SHA-256
  `766BD599DC2012EC79826CC54690276B46682D067CA8723E322A65A921A24226`.
  Телефон, `adb devices`, scrcpy, сборка и медиатракт пока не запускались.
- Статическая проверка показала, что локальный `scrcpy-server` является
  ZIP/APK-подобным Android server: внутри есть `AndroidManifest.xml`,
  `classes.dex` и `META-INF/version-control-info.textproto`; embedded revision
  указан как `dc8a55a24c27ae78ae202b5a3288da681d0f141a`. Официальный GitHub API
  для release `Genymobile/scrcpy v5.0.1` (published `2026-10-08`) сообщает для
  asset `scrcpy-server-v5.0.1` digest
  `sha256:764eb6f79811d5211fe9df341120882ba9994c7a61b897d7bf3fb662e53bc536`,
  совпадающий с локальным файлом. Это подтверждает server как официальный
  release asset v5.0.1; client/полный ZIP ещё не сверялись по archive digest.
- PE-инвентарь: локальный `scrcpy.exe` — PE32+ x64 (`Machine 0x8664`), bundled
  `adb.exe` — PE32 x86 (`Machine 0x014C`). Это не ошибка комплекта, но при
  проектировании внешнего C/C++ transport нельзя предполагать, что каждый
  компонент имеет одинаковую разрядность. Всего в папке 16 файлов, суммарно
  26206780 bytes; обычных кадровых файлов или runtime-данных там нет.
- После отдельного разрешения пользователя выполнена host-side smoke-проверка:
  bundled `adb.exe` запущен на отдельном порту `5038`, показал
  `Android Debug Bridge 1.0.41`, `Version 37.0.1-15733141`; bundled
  `scrcpy.exe --version` подтвердил `scrcpy 5.0.1`, SDL `3.4.18`, FFmpeg
  `63.1.102` и libusb `1.0.30`. `adb devices -l` на `5038` вернул пустой
  список, `get-state` — `no devices/emulators found`; телефон не был виден.
  Временный daemon `5038` после теста остановлен.
- По отдельному разрешению очищены чужие старые ADB-транспорты: ADB server на
  `5037` из временного scrcpy (`PID 19124`), ADB `screenrecord` старого
  `streamer_webrtc.py` и его дочерний server. Источник старого respawn был
  остановлен: `C:/Users/atsun/Documents/antigravity/busy-goodall/streamer/`
  `streamer_webrtc.py`. Итоговая проверка: `NO_ADB_PROCESSES`, порты `5037` и
  `5038` закрыты. Текущий BeamNG и synthetic producer не трогались.
- После подключения телефона bundled ADB обнаружил авторизованное USB-устройство:
  serial `00170155H000255`, product `AsteroidsPro`, model `A059P`, state
  `device`, transport id `1`. Read-only device properties: manufacturer
  `Nothing`, Android `16`, API `36`, physical display `1080x2392`, density
  `420` с override `375`, rotation `0`.
- Для терминологии медиатракта зафиксировано: scrcpy получает video и audio
  с устройства, а touch реализует как host-side control injection на Android.
  Это не обратный sensor-capture всех физических касаний телефона: наш Lua
  inverse-UV mapping формирует один pointer `DOWN/MOVE/UP`, а scrcpy передаёт
  его в Android control channel.
- Реальный video smoke-test вне BeamNG пройден через bundled
  `scrcpy-win64-v5.0.1`: `scrcpy 5.0.1` передал официальный
  `scrcpy-server` на телефон, поднял live video без `--record`, использовал
  renderer `direct3d11`, interop/decoder `d3d11va`, получил texture `336x720`.
  Сессия прожила 8 секунд и завершилась после теста; файловый video transport
  не применялся.
- Отдельный audio-only smoke-test с `--no-video` и без компьютерного playback
  штатно завершил 5-секундную запись в `Z:` (ImDisk RAM), получил Matroska
  audio-файл размером `84559` bytes и затем удалил его. На обычный постоянный
  диск audio/video запись не выполнялась. Это подтверждает ADB/server/audio
  capture path, но ещё не согласование PCM с игровым звуковым трактом.
- Control smoke-test запустил scrcpy с live video, поднял `Controller thread`,
  отправил один синтетический mouse `DOWN/UP` в центр окна и завершил процесс.
  Лог подтвердил `Controller stopped`, `Device message sender stopped` и
  `Screen streaming stopped`. Это подтверждает инициализацию control channel и
  lifecycle, но без Android UI-индикатора не доказывает действие конкретного
  приложения. Android multi-touch не использовался.
- С учётом runtime proof связка
  `scrcpy video → ImDisk RAM → BeamNG VFS → ImTextureHandler → AddImageQuad`
  принята как рабочий MVP consumer path. Прямой zero-copy/native texture
  upload не является обязательным gate; вернуться к нему можно только как к
  оптимизации после закрытия функциональных video/audio/control требований.
- После smoke-тестов scrcpy-процессы отсутствуют; bundled ADB server на `5037`
  оставлен активным для авторизованного устройства. Synthetic producer и
  BeamNG не останавливались и не изменялись.

### Подготовка runtime-проверки WebView → material

- В `prototype/android_probe_mod/vehicles/common/android_probe/
  android_probe.materials.json` и совпадающей установленной копии добавлена
  явная привязка `emissiveMap: "@beam_auto_android_probe_screen"` по шаблону
  существующего локального `beamscreenstream` probe. Однотонная зелёная
  диагностика заменена на тёмную emissive-базу с WebView map.
- JSON обеих копий прошёл `ConvertFrom-Json`; SHA-256 исходника и установленной
  распакованной копии совпадает: `41609A1C925B30F865CF91A719361CA46BB313221CA62DE3F29EA53328FC949B`.
- На момент записи BeamNG.drive был запущен, но после изменения material ещё не
  выполнялся ручной `Ctrl+L`/пересоздание машины. Runtime-результат ожидает
  визуальной проверки пользователя. Телефон, ADB и scrcpy не подключались.
- Пользователь сообщил, что после установки probe у Vivace были повреждены
  штатные nodes/кузов. Причина подтверждена в исходном JBeam: probe добавлял
  четыре узла с `nodeWeight: 0.7` и соединял их со штатными `rf1r/rf1l/f5r/f5l`
  балками с жёсткостью до `800000` и прочностью до `60000` (`vehicles/vivace/
  android_probe_vivace.jbeam:24–46`). Это было недопустимое физическое
  вмешательство, а не следствие material map.
- Для немедленной защиты машины из обоих probe JBeam удалены `nodes`, `beams` и
  `flexbodies`; controller/WebView-конфигурация сохранена. Исходный и
  установленный JBeam совпадают: Vivace adapter SHA-256
  `79011DFA022B45BA5AF1E0C59309BA67A2A778061C5503C88F49104A9A8EC810`, общий
  part SHA-256 `7043A7AE97E2DC29F1023D2BB3D51C6686C64294C55687434E751D747C04A431`.
  Все четыре JBeam-файла прошли `ConvertFrom-Json`; поиск физической секции
  probe пуст. Это временно отключает физический carrier, но оставляет ImGui и
  WebView controller для безопасной диагностики.
- Уже повреждённую машину этот файл автоматически не восстановит: после
  перечитывания JBeam нужно создать/перезагрузить машину или перезапустить
  уровень и проверить новый чистый экземпляр. Телефон, ADB и scrcpy не
  подключались.
- Пользователь подтвердил повторную проверку: машина больше не ломается, но
  изображение на экране не изменилось. Свежий лог подтверждает выбор
  `beam_auto_android_probe_vivace` в `licenseplate_design_2_1` (`beamng.log:
  2653, 3248–3249`) и создание WebView `@beam_auto_android_probe_screen`
  512x1024 (`beamng.log:3298`). Одновременно GE-extension продолжает рисовать
  прежний ImGui carrier (`beamng.log:3336`), что и объясняет неизменившийся
  экран на скриншоте.
- Это не является положительным тестом WebView → material: в текущем JBeam
  (`prototype/android_probe_mod/vehicles/vivace/android_probe_vivace.jbeam:8–18`
  и `vehicles/common/android_probe/android_probe.jbeam:8–18`) после защитного
  исправления остался только controller, а `flexbodies` отсутствуют. Значит,
  WebView создан, но физического mesh-потребителя для `emissiveMap` больше нет.
  Старые строки импорта `android_probe.dae` относятся к предыдущему варианту
  probe и не доказывают текущий material path. Результат: физическая проблема
  устранена, WebView → собственный физический material остаётся
  неподтверждённым; телефон, ADB и scrcpy не подключались.

### Bounded-аудит WebView stream и внешней texture upload

- Разрешённая пользователем итерация выполнена только чтением локальных Lua,
  модовых примеров, бинарных строк и журнала; телефон, ADB, scrcpy, сборка и
  новые JBeam-физические связи не запускались.
- `lua/vehicle/htmlTexture.lua:7–14,17–35,38–58` показывает точный контракт:
  `queueWebViewJS` и `queueWebViewStreamJS` получают строку JavaScript, которая
  строится через `jsonEncode(data)`. `streamJS` не принимает указатель,
  `ArrayBuffer`, stride, формат пикселей или shared texture; это очередь
  структурированных JS-вызовов.
- Штатные вызовы `streamJS` найдены только для небольших данных приборов и
  карты: `lua/vehicle/controller/gauges/genericGauges.lua:79–93`,
  `gauges/customModules/navigationData.lua:14–24` и
  `analogOdometer.lua:39`. Это подтверждает data-stream, но не видеобуфер.
- `ImTextureHandler` в
  `lua/common/extensions/ui/imgui_custom_luaintf.lua:199–207` только создаёт
  handler и задаёт ему строковый ID через `setID(path)`. В Lua-обвязке нет
  функции upload/update texture из RAM, параметров `width/height/stride/format`
  или импорта external/shared resource.
- `lua/common/extensions/ui/imgui_luaintf.lua:2088–2100` подтверждает, что
  `AddImageQuad` принимает уже существующий `ImTextureID`; сам вызов не
  загружает пиксели.
- Найденный `Research.SharedMemoryManager` используется в
  `lua/ge/extensions/tech/capturePlayer.lua:354–387,450–494` для чтения
  shared-memory результатов BeamNG.tech sensors. Это consumer данных сенсоров,
  а не импорт внешнего изображения в material или ImGui.
- Локальный `beamscreenstream` (`vehicles/vivace/
  beamscreenstream_vivace.jbeam:9–22`, `lua/vehicle/controller/
  beamScreenStreamScreen.lua:4–14`, `vehicles/common/beamscreenstream/
  screen.html:22–58`) содержит только синтетический Canvas с
  `requestAnimationFrame` и WebView material tag. Входного RAM-кадра,
  декодера, сокета или ADB там нет. Его nodes/beams повторяют ранее признанную
  небезопасную схему и для эксперимента не использовались.
- Дополнительный поиск бинарных строк в `Bin64` не дал подтверждённого
  публичного texture consumer; широкое сканирование задело Chromium/WebCore и
  остановилось по тайм-ауту, поэтому внутренние строки не считаются API.

Вывод bounded-аудита: `queueWebViewStreamJS` может быть полезен для управления
WebView и небольших метаданных, но не предоставляет подтверждённый канал
`RAM video → WebView/material`. Direct `external/shared texture → BeamNG`
также не найден, но это больше не блокирует MVP: выбран и подтверждён
ImDisk RAM-backed VFS bridge. scrcpy уже подключён и проверен отдельно;
следующий шаг — заменить synthetic producer на реальный scrcpy producer.

Источники: исследовательские сессии из `docs/TEAM.md`, перечисленные локальные
файлы и https://github.com/Genymobile/scrcpy/blob/master/doc/develop.md.
Историческая bounded-аудитная запись о незапущенных примерах относится только к
тому исследовательскому проходу; позднее телефон и scrcpy прошли отдельный
runtime smoke-test, описанный выше.

## Неопределённости
- Доступный способ связать RAM/WebView с ImGui на подтверждённом D3D12.
- Доступный способ связать material tag собственного JBeam-part с Vehicle
  WebView/canvas и передать в него кадровый поток без файлов.
- Версия Android Nothing 3a Pro; USB выбран базовым подключением.
- Нужна ли дополнительно поддержка wireless ADB.
- Конкретная версия и объём адаптации scrcpy, состав поставляемых зависимостей.
- Реальный интерфейс создания/обновления RAM-текстуры в BeamNG.
- Возможность сохранить текущий звуковой тракт без зависимостей от Vivace.
- Требования к FPS, разрешению и задержке.
- Какие части каждого примера лучше подходят после сравнения.
- Происхождение и условия повторного использования фрагментов другого мода.

## Следующий шаг
MVP gate `synthetic JPEG → ImDisk RAM → BeamNG VFS → ImTextureHandler →
AddImageQuad → vehicle-relative perspective projection` пройден. Телефон и
bundled scrcpy отдельно подтвердили внешний video/audio/control transport.
Следующий цикл:

1. Заменить synthetic producer на scrcpy video producer, сохранив ImDisk RAM,
   dynamic junction, metadata retry и текущую ImGui projection.
2. Проверить scrcpy producer lifecycle: stop/restart, USB reconnect, удаление
   RAM-диска/junction, `Ctrl+L` и повторное создание texture handler без stale
   ресурсов.
3. Передать локальный inverse-UV один-контактный `DOWN/MOVE/UP` в scrcpy
   control channel и сделать reset активного pointer при disconnect.
4. Адаптировать audio sink отдельной веткой, сохраняя рабочий механизм
   исходного примера и динамические PCM metadata.
5. Direct zero-copy/native texture upload не считать gate и не исследовать до
   рабочего MVP; после MVP это только performance optimization.

### Подтверждённая проверка прямого ADB video

- 2026-10-19 пользователь отдельно запросил проверку получения кадров напрямую
  через ADB, без захвата окна scrcpy.
- На подключённом Nothing A059P (`00170155H000255`) bundled
  `scrcpy-win64-v5.0.1/adb.exe` выполнил:
  `adb -s 00170155H000255 exec-out screenrecord --time-limit 3
  --output-format=h264 -`.
- Команда завершилась с `EXIT=0`, вывела `59385` bytes. Начало потока было
  `00 00 00 01 67 ...` (H.264 SPS), затем `68` (PPS) и `65` (IDR). Это
  подтверждает настоящий Annex-B H.264 stdout-поток, а не запись на обычный
  диск и не содержимое окна.
- Повторная 5-секундная проверка дала `69072` bytes примерно за `5566 ms`.
  Поток содержал start codes; точный FPS и PTS ещё не измерены.
- `adb shell screenrecord --help` на этом Android 16 показывает обычный
  filename-интерфейс и не документирует `--output-format`, однако фактический
  `exec-out ... --output-format=h264 -` работает. Это runtime-факт для текущего
  устройства/прошивки, а не универсальная гарантия всех Android.
- Вариант `... screenrecord /dev/stdout` не принят: stdout содержит текст
  `ERROR: couldn't remove existing file` и `Aborted`. Для бинарного потока
  использовать именно `exec-out` и `--output-format=h264 -`.
- Прямой ADB video не заменяет scrcpy audio/control: `screenrecord` даёт только
  video. Для MVP остаётся открытым выбор между отдельными ADB video/audio/control
  ветками и согласованным scrcpy transport; оконный GDI bridge отклонён после
  пользовательской проверки, потому что он захватывал участок монитора.
- Архитектурная гипотеза для следующего implementation gate:
  `adb exec-out screenrecord → C/C++ H.264 decoder → JPEG encoder → ImDisk
  RAM/VFS`. В raw Annex-B потоке не подтверждены PTS; для A/V sync и стабильной
  задержки нужно отдельно измерить arrival timing либо предпочесть scrcpy
  packet metadata.

### Native ADB H.264 → JPEG producer

- После явного разрешения пользователя создан отдельный C++ producer:
  `prototype/android_probe_mod/native/adb_h264_jpeg_producer.cpp`.
- Producer запускает bundled `adb.exe` как дочерний процесс, читает
  `exec-out screenrecord --output-format=h264 -` через bounded pipe, разбирает
  Annex-B H.264 через FFmpeg parser/decoder, конвертирует decoded frame через
  libswscale и кодирует JPEG через libjpeg-turbo.
- Публикация кадра атомарная: `.tmp` → `MoveFileEx(...REPLACE_EXISTING)`.
  Размер/ориентация телефона не зашиты; `width/height` берутся из `AVFrame`.
- Реальный smoke-test на `00170155H000255` прошёл: 10 кадров, `1080x2392`,
  JPEG до `166994` bytes, `result=0`, RAM-диск и staging после теста удалены.
- Добавлен coordinator
  `prototype/android_probe_mod/tools/adb_h264_imdisk_producer.ps1`: динамически
  выбирает букву, создаёт ImDisk и junction, запускает native producer,
  передаёт handshake в текущий Lua contract и удаляет ресурсы при остановке.
- `--max-frames 0` означает бесконечный lifecycle и не является FPS.
  Добавлен отдельный `--fps 1..120`, по умолчанию `60`; он ограничивает частоту
  JPEG/VFS publication, тогда как H.264 input продолжает приниматься.
- Контрольный тест `--fps 30 --max-frames 10` прошёл с `EXIT=0`, metadata
  содержит `outputFps=30`, последний кадр `1080x2392`, `MOUNT_AFTER=False`.
- Native build использует MSYS2 UCRT64 FFmpeg 9.0.2 и libjpeg-turbo 3.2.0;
  runtime DLL должны находиться в окружении процесса (`C:\msys64\ucrt64\bin`).
- Пользователь подтвердил runtime end-to-end проверку: при запущенном native
  ADB producer-е на `60 FPS` выполнил `Ctrl+L`, после чего Lua заново создал
  texture handler и реальный экран телефона появился в vehicle-relative
  перспективной ImGui-проекции. Во время проверки producer имел
  `status=running`, `1080x2392`, растущий sequence и живую junction.
- Это закрывает текущий video gate:
  `ADB exec-out H.264 → C++ decode → JPEG → ImDisk/junction → BeamNG VFS →
  ImTextureHandler → AddImageQuad`. Ошибки загрузки texture до `Ctrl+L`
  относятся к stale handler предыдущей сессии и не считаются отказом нового
  ADB producer-а; lifecycle повторной загрузки Lua проверен пользователем.
- По запросу пользователя добавлен выбор масштаба video/output в native C++ GUI
  и producer: `0.25`, `0.5`, `0.75`, `1`; default `0.5`. При коэффициенте ниже
  `1` producer запрашивает размер устройства через `adb shell wm size`, передаёт
  пропорциональный `screenrecord --size`, уменьшает bitrate примерно по площади
  и публикует итоговый JPEG. Если запрос размера не удался, используется
  fallback-масштабирование decoded `AVFrame` через libswscale.
- Runtime-проверка нового ADB-запроса на Nothing A059P (`00170155H000255`)
  вернула `Physical size: 1080x2392`; сборка producer/GUI прошла успешно.
  Это подтверждает получение исходного размера динамически, но отдельный
  короткий scaled end-to-end прогон через ImDisk ещё не выполнен.
- После визуальной проверки GUI исправлен автоподхват BeamNG config: раньше поле
  заполнялось только при наличии `BEAMNG_FRAME_CONFIG`, поэтому при обычном
  запуске оставалось пустым. Теперь native GUI сначала уважает эту переменную,
  затем проверяет `BEAMNG_USER_PATH`/`BEAMNG_USER_DATA`/`BEAMNG_HOME`, текущий
  каталог и его родителей, а также root-level BeamNG user-data каталоги на
  доступных дисках; найденный файл — `temp/beam_auto_android_frame_probe.json`.
  Буква диска и имя пользователя не зашиты. Одновременно исправлено наложение
  кнопок Start/Stop на новый Scale combo.
- Пользователь подтвердил, что на первом этапе допустимо сохранить старый
  HTTP-контракт управления `tap/swipe` вместо немедленного перехода на прямой
  scrcpy `DOWN/MOVE/UP`. Согласованный вариант: оставить Lua inverse-UV и
  gesture semantics старого мода, но не возвращать Python runtime; HTTP listener
  и очередь команд должны быть native C++ bridge с loopback-only transport.
- Пользователь согласовал перенос полного старого placement UI в vehicle-relative
  ImGui carrier: перемещение, масштаб, глубина и сохранение настроек по машине.
  Кнопка Flip и связанные с ней визуальные/горячие действия исключены.
- Для audio согласован старый Web Audio sink с spatial panner и muffling; источник
  PCM должен подключаться через native C++ transport с bounded/reconnect lifecycle.
- По просьбе пользователя 2026-10-19 обновлена установленная BeamNG-копия:
  удалены старый распакованный `D:\BeamNGUSER\current\mods\unpacked\beam_auto_android_probe`
  (Vivace/JBeam/WebView carrier) и отдельный диагностический
  `android_texture_probe`; возможный packed
  `D:\BeamNGUSER\current\mods\beam_auto_android_probe.zip` также удалён.
  Затем установлен актуальный `prototype/android_probe_mod` как единственный
  распакованный `beam_auto_android_probe`. Фактический состав установки:
  `mod_info.json`, `scripts/beam_auto_android_probe/modScript.lua`,
  `lua/ge/extensions/beam/auto/android/probe.lua` и `audio.lua`; каталог
  `vehicles/` отсутствует, поиск Vivace/JBeam/Flip по установленной копии
  совпадений не дал. Отдельный пользовательский `roadwave_music_player` не
  удалялся, поскольку это самостоятельный источник музыки/аудиокода.
- После пользовательской проверки обнаружен баг движения ImGui carrier-а:
  старый код в `prototype/android_probe_mod/lua/ge/extensions/beam/auto/android/probe.lua`
  заменял текущие позиции vehicle nodes предыдущими, пока расстояние было
  меньше 5 м (`squaredDistance(previousRef) < 25`). Это замораживало экран на
  месте и создавало скачок после пересечения порога. Условие удалено; basis и
  world anchor теперь строятся по текущим node-позициям на каждом render tick.
  В тот же файл добавлены настоящее скругление корпуса и маскирование четырёх
  углов текстурного окна цветом корпуса в перспективном quad. Исправленный
  файл синхронизирован с установленным модом, SHA-256 source/installed:
  `5B8317C21AE2B998B7184972F8A10DA41C8EA757350C69D5FD966C3A1FF94647`.
- По следующему уточнению пользователя уменьшен визуальный bezel в
  `probe.lua`: экранный inset изменён с `0.08/0.92, 0.06/0.94` на
  `0.035/0.965, 0.03/0.97`, толщина рамки снижена до `1 px`. Постоянные строки
  `TOUCH/UV/FRAME/TOUCHQ/LHD` убраны. Добавлена кнопка `Debug`, которая по
  запросу раскрывает компактную диагностическую панель; рядом оставлена только
  кнопка `Move`, Flip не добавлялся. Исправленный установленный файл совпадает
  с исходником SHA-256:
  `C7C7489450165752F120DE03529CAE0629B9127B37B35CCF538770606BF2DE45`.
- По следующему runtime-симптому проверен anchor с `roadwave_music_player.zip`:
  RoadWave намеренно рисует carrier по предыдущему physics-сэмплу ref/left/back
  узлов, потому что render сцены отстаёт от свежего состояния GE Lua примерно на
  кадр. В `probe.lua` возвращена именно эта одно-кадровая фиксация без прежнего
  порога `squaredDistance < 25`: текущий сэмпл сохраняется для следующего кадра,
  а предыдущий используется для текущего draw. Источник сравнения:
  `roadwave_music_player.zip:lua/ge/extensions/rw/device.lua`, комментарий перед
  `prevRefP/prevLeftP/prevBackP` и участок anchor render; старый freeze-порог
  probe был зафиксирован в `PROJECT_MEMORY.md:803–808`.
- По сообщению пользователя обнаружена и исправлена ориентация video carrier-а.
  Native producer больше не передаёт в `screenrecord` фиксированный `--size`:
  при изменении ориентации Android поток может менять `AVFrame width/height`.
  Lua читает эти размеры из metadata, меняет portrait/landscape габариты
  vehicle-relative quad и синхронно меняет физическую touch-систему координат.
  Текущий установленный `probe.lua` совпадает с исходником SHA-256:
  `E253EC1C7029941DED7E4FDF8277F202E73784C25DA8851C6D65AAA8C023290C`.
- Кнопки `Debug` и `Move` больше не рисуются постоянно: они показываются только
  при наведении курсора в узкую activation-зону над верхним краем экрана и
  размещаются в диапазоне `v=-0.20..-0.04`, чтобы не перехватывать обычный
  Android touch. Состояние `Debug` и placement `Move` сохраняются прежними.
- По запросу пользователя в `native/adb_h264_media_gui.cpp` добавлена кнопка
  `Rotate`. Она переключает выбранное Android-устройство между `0°` и `90°`
  командой `adb shell wm user-rotation lock`, учитывая заданные в GUI ADB path
  и Serial. После сборки актуальный GUI находится в
  `prototype/android_probe_mod/native/build/beam_auto_android_media_gui.exe`,
  SHA-256:
  `97DB15B562EF1C132AEEFB4A48DC5F588C76E7964BF39E4DD9841B5552F6F5DD`.
  Для применения новой кнопки нужно закрыть старый GUI и запустить этот EXE;
  уже работающий процесс сам новую сборку не подхватывает.
- После добавления `Rotate` GUI начал останавливаться сразу после Start с
  handshake `frameNumber=0`. Проверка показала на телефоне несколько десятков
  зависших удалённых `screenrecord`; новый Android encoder возвращал текст
  `ERROR: unable to start codec`, хотя ADB завершался с кодом `0`. В producer
  добавлена очистка `adb shell pkill -9 screenrecord` перед и после сессии,
  проверка текстовой ошибки потока и ошибка при завершении без единого кадра.
  После пересборки короткий runtime smoke-test прошёл: `FRAME sequence=1
  size=540x1196`, `PRODUCER_STOPPED frames=1 result=0`; на телефоне осталось
  `0` процессов `screenrecord`. Актуальные SHA-256 сборок:
  producer `37637001BCC509EECC...`, GUI `00AF4AE8FC276C9C71...`.
- По следующему запросу в `probe.lua` добавлена третья hover-кнопка `Hide/Show`.
  Она использует ту же activation-зону над верхним краем carrier-а, скрывает
  весь корпус/текстуру и отключает touch с reset активного жеста; повторное
  наведение и `Show` возвращает экран. Исходник синхронизирован с установленной
  копией, SHA-256 текущего Lua-файла:
  `A88F39CBBCB79DF37F114C80351B741777EB53AF12F79BA32FD6F1D99D5969F7`.
- Подготовлена финальная раздельная поставка в `release/`: мод отдельно от
  native runtime. `beam_auto_android_probe_mod.zip` содержит только `mod_info.json`,
  `scripts/`, `lua/ge/.../probe.lua`, `lua/ge/.../audio.lua` и служебные пустые
  каталоги; native EXE/DLL, JBeam/Vivace и исходники в него не входят.
  `beam_auto_android_media_runtime.zip` содержит GUI, producer, рекурсивный
  комплект из 81 импортируемой producer DLL, bundled `scrcpy-win64-v5.0.1`, оба
  README и вложенный `dependencies/ImDisk/ImDiskTk64.zip`.
- В runtime-поставку добавлен английский README
  `release/README_BEAM_AUTO_ANDROID_EN.md`; русский README дополнен установкой
  ImDisk из вложенного архива и описанием `Hide/Show`. Оба README проверены в
  распакованном runtime ZIP и совпадают с исходными файлами.
- Финальная проверка архивов 2026-10-19: мод — 13 582 bytes,
  SHA-256 `10FFF4CF138B1E828E967FA279DF4C0D2479F5FEC28D0B166DD876BF0C8EF194`;
  runtime — 58 610 120 bytes,
  SHA-256 `20095025E9C8F6B2B524422E3C5174BE4F513907E20B99BFC6B3AB823FA5963C`.
  Вложенный исходный архив ImDisk: 799 789 bytes,
  SHA-256 `AB309F8C5C810EAE5AEC874F070751835234199BCD194D84CDF572F553CCFB3F`.

Старый экспериментальный пункт WebView → физический material остаётся
отдельной гипотезой, но не является ближайшим шагом для уже подтверждённого
ImGui video consumer. Физические nodes/beams/flexbodies к Vivace не возвращать.

Историческая гипотеза WebView → физический material проверяется только через
безопасный **visual-only attachment** без добавления массы и силовых балок к
штатным узлам:

1. Найти штатный JBeam visual attachment pattern, который не добавляет
   физические nodes/beams/mass и не связывается с `rf1r/rf1l/f5r/f5l`.
2. На отдельном собственном mesh проверить WebView material с движущимся
   Canvas; старую опасную Vivace-схему не возвращать.
3. Проверить обновление, повторное создание и освобождение WebView/material.
4. Не подключать телефон, ADB или scrcpy и не использовать файловые кадры.

Если этот путь не подтвердится, считать штатный внешний видеопотребитель для
целевой 3D-проекции не найденным и вынести пользователю архитектурный выбор.
Native D3D12 hook, DLL-инжект и вызовы внутренних адресов в следующий шаг не
включать.
После ручного `Ctrl+L` убедиться, что диагностический пробник
перестаёт опрашивать ресурс.
Уточнить путь MinGW, допустимость build-time Python/Meson перед сборкой,
Android и требования к качеству при подготовке медиатракта.
При разрешении реализации использовать live-install и `Ctrl+L` как быстрый
цикл проверки Lua, но отдельно проверять полную выгрузку/пересоздание ресурсов.
До подключения телефона/scrcpy и разработки универсального anchor использовать
только этот изолированный Vivace carrier probe. Для runtime-проверки выбрать
   `Beam Auto Android Probe` в слоте `License Plate Design`, загрузить конфигурацию
   Vivace и проверить в логе создание `androidProbeScreen`/WebView и видимость
   физического flexbody в driver camera. После добавления JBeam сначала повторно
   активировать мод или перезапустить игру, чтобы BeamNG заново упаковал
   распакованную копию с `vehicles/vivace/android_probe_vivace.jbeam`; одного
   `Ctrl+L` недостаточно для обновления состава JBeam. После этого отдельно
   добавить прозрачный ImGui input layer и лишь затем решать вопрос
   универсального attachment.
