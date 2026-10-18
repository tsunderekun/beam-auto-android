# Исследование установленной игры и scrcpy

Дата: 2026-10-18. Только чтение локальных файлов, существующего журнала и
upstream-исходников. Игра, телефон и прототипы в рамках проверки не запускались.
Выводы из кода не означают проверенную работоспособность медиатракта.

## 1. Версия и фактический renderer

Источник: `D:\BeamNGUSER\current\beamng.log` на момент исследования:
- строки 1–2: BeamNG.drive 0.39.4.0 x64, build 20972;
- строки 162–180: проверка возможностей и выбор DirectX 12;
- строки 183–199: создание GFX D3D12 device, swap chain и GPU
  NVIDIA GeForce RTX 5070.

Это подтверждение backend по журналу существующего запуска, а не только по
наличию DLL. Проектировать нативный мост исключительно под D3D11 нельзя.
Отдельная поддержка других backend пока не согласована.

## 2. Native / ImGui / FFI

Все пути этого раздела относительно
`D:\SteamLibrary\steamapps\common\BeamNG.drive`.

- `lua/common/extensions/ui/imgui_custom_luaintf.lua:199–207`:
  `ImTextureHandler(path)` создаёт handler и вызывает `setID(path)`.
- `lua/common/extensions/ui/imgui_luaintf.lua:374–379,2094–2100`:
  `Image` и `ImDrawList_AddImageQuad` принимают уже существующий texture ID.
- `lua/ge/extensions/editor/renderViewBuffers.lua:75–105`:
  именованный render target передаётся как `#name`, backbuffer как `#backbuffer`,
  затем используются `getID/getSize/getFormat`.
- `lua/common/cdefImgui.lua:5–7` описывает callback текстового ввода, не upload API.
- `lua/common/cdefGpuMesh.lua:5–12,51–80`: mesh-структуры не дают texture upload API.
- `lua/ge/ge_utils.lua:225–277`: пример `GBitmap` с `init/setColor/saveFile/loadFile`;
  связи GBitmap → ImTextureID в исследованном Lua не найдено.

**Наблюдение:** в исследованной Lua-обвязке не найден прямой метод
RGBA/JPEG bytes → ImGui texture. Это не доказательство отсутствия любых
внутренних native API движка. Наличие FFI или произвольной DLL не решает вопрос
доступа к graphics device, регистрации texture ID и render-thread lifecycle.

`ImTextureID` считать opaque ID: его фактическое представление не установлено.
Нельзя подставлять туда адрес из другого процесса, D3D shared handle или
предполагаемый D3D11 SRV. MinGW также не гарантирует совместимость с C++ ABI игры.

Именованные render targets — подтверждённый по исходнику потребитель для ImGui;
способ наполнения такого target Android-кадрами требует отдельной проверки.

### 2.1.1. Что найдено в бинарнике и почему это ещё не bridge

В ASCII-строках `Bin64/BeamNG.drive.x64.exe` обнаружены имена внутренних
компонентов графического движка, в частности:

- `engine::GFXTextureManager::createTexture` и `_createTexture`;
- `engine::GFXd3d12Device::uploadTextureData`;
- `engine::GFXTextureObject`, `engine::NamedTexTarget`;
- `engine::TextureDrawPrimitiveRegistry` и `TextureDrawPrimitive`;
- `engine::TheoraTextureObject`;
- `engine::GameEngineLua::registerLuaApisGfx`.

Это **наблюдение по строкам бинарника**, а не доказательство доступного API:
имена не являются документированной ABI-точкой входа. В таблице PE-экспортов
EXE релевантного публичного upload/render entrypoint не найдено; из связанных
экспортов остался только `ImGuiInputTextCallbackLua`. Вызов внутренних C++
методов по адресам, восстановленным из дизассемблирования, потребовал бы
неподтверждённых сигнатур, layout классов, allocator/RTTI-совместимости и
синхронизации с render thread. Такой DLL-инжект не считать допустимым
нативным адаптером мода.

`TextureDrawPrimitiveRegistry` — реальный Lua-потребитель для синтетической
геометрии: `lua/ge/extensions/util/materialDebugViz.lua:43–115` создаёт
примитив, рисует градиент/линии и выводит его через `td:ImGui_Image`; похожий
путь есть в `lua/ge/extensions/ui/apps/minimap/minimap.lua:464–492`. В
исследованных вызовах нет метода, принимающего внешний RGBA/ARGB-буфер или
обновляющего текстуру из RAM. Найденная в бинарнике
`TextureDrawPrimitive::queueBlitTextureFile` по названию указывает на файловую
операцию и в любом случае не удовлетворяет требованию не использовать файлы
кадров.

`TheoraTextureObject` и `setNamedTexTarget` также видны во внутренних строках,
но в Lua-исходниках проекта не найдено рабочего создания/подачи внешнего
потока в этот объект. Наличие Theora-класса не доказывает подходящий для
scrcpy вход и не раскрывает его lifecycle.

### 2.1.2. Поддержанные Lua render-пути, проверенные статически

`RenderViewManagerInstance:getOrCreateView()` создаёт собственный render view,
а `renderView.namedTexTargetColor` задаёт именованный target, который затем
читается как `imUtils.texObj('#' .. renderViewName)`:

- `lua/ge/extensions/render/viewDemo.lua:28–69`;
- `lua/ge/extensions/render/renderViews.lua:75–107`;
- `lua/ge/extensions/editor/renderViewBuffers.lua:75–105`.

Но источник пикселей этого target — камера/сцена BeamNG. Ни один из этих
вызовов не принимает буфер внешнего процесса. `vehicle:renderCameraToMaterial`
в `lua/ge/extensions/core/vehicleMirrors.lua:59–66` аналогично рендерит камеру
в материал, а не импортирует Android-кадр.

Отдельный DLL-пример `lua/ge/extensions/tech/rawLidar.lua:23–55,57–104`
показывает только sensor-specific FFI-контракт: он доступен при подтверждённой
лицензии BeamNG.tech и отключённом sandbox. Это не общий plugin API графики и
не даёт доступа к D3D12 device, texture registry или ImGui `ImTextureID`.

**Промежуточный вывод:** внутренние графические реализации в EXE есть, но
поддержанный и проверяемый native D3D12 bridge RAM → texture → ImGui в поставке
BeamNG.drive 0.39.4.0 не найден. Продолжать к scrcpy до появления такого
контракта означает строить медиатракт на неподтверждённом ABI.

## 2.1. HTML/WebView как возможный адаптер

- `lua/vehicle/htmlTexture.lua:7–15,17–35,38–59`: Vehicle Lua создаёт
  WebView через `obj:createWebView`, обновляет JS через `queueWebViewJS` и
  `queueWebViewStreamJS`; есть automatic/once/manual modes.
- `lua/vehicle/controller/gauges/genericGauges.lua:180–194,220–224`:
  штатный usage берёт имя из `materialName` и создаёт HTML-текстуру прибора.
- `lua/ge/ge_utils.lua:406–411`: **закомментированный** старый пример
  `playerVehicle:createUITexture("@licenseplate", ...)`,
  `queueJSUITexture`, `destroyUITexture`. Это направление поиска, но не
  подтверждение наличия методов в текущем runtime и не рабочий lifecycle API.

Связь WebView → именованный render target → ImTextureHandler **не установлена**.
Ни `#name`, ни `@name`, ни webViewTag не считать взаимозаменяемыми.
Также не установлены актуальные resize/destroy API и прямой доступ к
WebView из GE Lua без vehicle/material. Применение приборных экранов здесь
исследовано только как пример механизма, интеграция со штатным Vivace исключена.

В установленном RoadWave
`D:\BeamNGUSER\current\mods\unpacked\roadwave_music_player\lua\ge\extensions\rw\stream.lua:3–19`
автор описывает блокировку внешних CEF-загрузок, включая loopback, и тракт
`TCP → Lua → base64 → queueJS → MSE`. Строки 24–49 задают loopback-соединение,
тайм-аут и бюджеты чтения. Это наблюдение о коде и сообщения его автора,
не наша проверка сетевых ограничений CEF. WebSocket/HTTP из HTML автоматически
работающими не считать; не переносить поведение MainGEUI на Vehicle WebView.
Установленный RoadWave также не считать побайтово идентичным исходному архиву.

Пространственный WebAudio RoadWave в GE Lua не требует штатного Vivace по
структуре исследованного кода. Готового PCM-входа именно в RoadWave этим не
подтверждено: его stream-модуль принимает сжатое радио через MSE, тогда как
Android backup имеет отдельный PCM-тракт. При переносе сохранять пригодные
компоненты обоих примеров, явно согласовав формат аудиовхода.

### Предлагаемая первая практическая проверка (только после разрешения)

Сначала синтетическое движущееся изображение, без телефона и scrcpy:
1. Проверить возможность создать/обновлять WebView подходящим доступным API,
   не меняя существующие моды и штатные экраны автомобиля.
2. Установить, можно ли получить именно его texture ID в GE Lua и передать в
   отдельный `AddImageQuad`; проверять не просто valid ID, а совпадение и движение
   контрольного изображения. Подтвердить отсутствие файлов кадров.
3. Проверить завершение/повторное создание, смену автомобиля и `Ctrl+L`.
4. Только если путь подтверждён — добавлять внешний транспорт и реальное видео.

Если невозможно создать независимый источник либо получить его ID, тест
HTML-пути остановить. Не превращать его в несогласованную интеграцию с
материалами конкретных машин. Затем отдельно исследовать поддержку нативного
адаптера D3D12; такой адаптер пока не гарантирован.

## 3. Найденный VideoStream runtime

В `Bin64/VideoStream` есть `libbngvs.dll`, FFmpeg DLL и исходный архив
`libbngvs-0.8.1-source.zip`. Архив прочитан без распаковки.

Внутри архива:
- `libbngvs-0.8.1/README.md:3–6`: библиотека принимает GPU-кадры игры,
  кодирует H.264/Opus и отправляет результат в файл/сеть;
- `README.md:49–56`: контракт D3D12 capture использует device/queue движка;
- `include/libbngvs/libbngvs.h:100–124`: `init`, output sinks,
  `stream_frame_gpu` для кодирования.

Это **исходящий захват игры**, а не обнаруженный интерфейс входящего видео
или создания ImGui-текстуры. Его наличие не закрывает RAM → texture.

## 4. scrcpy: конкретные точки адаптации

Исследован tag `v5.0.1`; это кандидат для фиксации пары client/server,
а не уже установленная зависимость проекта.

Первичные источники:
- https://github.com/Genymobile/scrcpy/releases/tag/v5.0.1
- https://github.com/Genymobile/scrcpy/blob/v5.0.1/meson.build
- https://github.com/Genymobile/scrcpy/blob/v5.0.1/app/meson.build
- https://github.com/Genymobile/scrcpy/blob/v5.0.1/app/src/trait/frame_sink.h
- https://github.com/Genymobile/scrcpy/blob/v5.0.1/app/src/trait/packet_sink.h
- https://github.com/Genymobile/scrcpy/blob/v5.0.1/app/src/audio_player.c
- https://github.com/Genymobile/scrcpy/blob/v5.0.1/app/src/controller.h
- https://github.com/Genymobile/scrcpy/blob/v5.0.1/app/src/control_msg.h
- https://github.com/Genymobile/scrcpy/blob/v5.0.1/doc/develop.md

Рекомендуемые границы расширения:
1. Video decoder → собственный `sc_frame_sink`: `push(AVFrame)`,
   `push_session` при смене размеров, ограниченный RAM-буфер.
2. Audio decoder → отдельный PCM `sc_frame_sink`: привести sample format,
   частоту и каналы к контракту сохраняемого Lua/WebAudio-тракта.
3. Lua mouse mapping → IPC → `sc_controller_push_msg` с
   `SC_CONTROL_MSG_TYPE_INJECT_TOUCH_EVENT` и одним pointer ID.
4. `sc_packet_sink` оставляет вариант передачи сжатых пакетов, если будет
   выбран браузерный декодер; это отдельное изменение предложенного пути.

ADB, запуск сервера, demuxer, decoder и сериализацию control переиспользовать.
Down/up не терять при объединении move-событий. При обрыве нельзя гарантировать
доставку up: локальное состояние сбрасывать и отдельно проверить восстановление
на стороне Android. При доступном соединении завершать контакт до shutdown.

В `doc/develop.md` этого tag остались примеры запуска и текст о протоколе 4.0.
Не копировать их номер версии/раскладку пакетов вслепую; приоритет у кода
выбранного tag и одинаковой версии server/client.

## 5. Сборочное окружение

По исследованию агента через `Get-Command`: gcc/g++, clang, cmake, meson,
ninja, pkg-config, make и adb не найдены в PATH. Git найден.
Проверены типовые каталоги C:\mingw*, C:\msys64, Program Files для
MinGW/LLVM/CMake/Meson/Android/Visual Studio; toolchain там не обнаружена.
Это не опровергает сообщение пользователя об установленном MinGW: он может
находиться по другому пути. Версия и target triple ещё неизвестны.

`meson.build`: C11, Meson >= 0.49. `app/meson.build`: FFmpeg
libavformat/libavcodec >= 60.3, libavutil >= 58.2, libswresample,
SDL3 >= 3.2.0, Windows mingw32/ws2_32. Для собственной конвертации пикселей
может дополнительно понадобиться libswscale. Зависимости libusb и hardware
interop зависят от выбранных опций. Игровые FFmpeg DLL не считать готовым
development-комплектом для сборки внешнего EXE.

Meson использует Python на этапе сборки:
https://mesonbuild.com/Getting-meson.html . Это не Python runtime будущего
медиапроцесса, но использование требует отдельного согласования согласно
правилам проекта. Ничего не установлено и не собрано.
