
[2026-01-29] Старт реализации MVP сервиса.
- Добавлен базовый Python-сервис FastAPI с конфигом через ENV, лимитом размера тела запроса и ограничением параллелизма.
- Реализованы модели запроса, валидации (units/trim/лимиты/проверка размещаемости) и обработка ошибок по контракту.
- Добавлен прототипный алгоритм раскладки (shelf-подход) с multi-start, seed, splitmix64 и выбором лучшего решения.
- Сгенерирован SVG по результатам раскладки (контур листа, прямоугольники деталей, подписи).
- Добавлен Dockerfile (multi-stage) и requirements.txt.
- Обновлен .gitignore (C++ артефакты сборки).

Файлы:
- .gitignore
- Dockerfile
- requirements.txt
- src/libnest2d_service/__init__.py
- src/libnest2d_service/app.py
- src/libnest2d_service/config.py
- src/libnest2d_service/errors.py
- src/libnest2d_service/models.py
- src/libnest2d_service/optimizer.py
- src/libnest2d_service/packing.py
- src/libnest2d_service/svg.py
- src/libnest2d_service/validation.py
- src/libnest2d_service/version.py

[2026-01-29] Подключение C++ ядра и обертки (pybind11) + подготовка сборки.
- Добавлен C++ модуль libnest2d_core (pybind11) со встроенным детерминированным shelf-алгоритмом, масштабированием координат (мм*1000), multi-start и таймаутом внутри C++.
- Добавлена Python-прослойка для вызова C++ ядра с обработкой ошибок и фолбэком на Python-реализацию.
- Подготовлены CMakeLists.txt и pyproject.toml для сборки нативного модуля через scikit-build-core.
- Dockerfile обновлен для сборки wheel проекта и установки нативного модуля в runtime.
- Валидации скорректированы: проверка размещаемости без учета spacing на границах.
- .gitignore дополнен артефактами C++ (pyd).

Файлы:
- .gitignore
- CMakeLists.txt
- Dockerfile
- pyproject.toml
- cpp/libnest2d_core.cpp
- src/libnest2d_service/core.py
- src/libnest2d_service/optimizer.py
- src/libnest2d_service/validation.py

[2026-01-29] Подготовлена интеграция libnest2d как внешней зависимости.
- Добавлен альтернативный C++ модуль libnest2d_core_lib.cpp с вызовом libnest2d::nest и преобразованием результата в placements.
- CMake обновлен: FetchContent для libnest2d (tag 5.0.0), параметры сборки (clipper/optimlib/header-only), выбор исходника модуля по наличию libnest2d_headeronly.
- Dockerfile дополнил зависимости сборщика (git, libboost-dev) для сборки libnest2d.

Файлы:
- CMakeLists.txt
- Dockerfile
- cpp/libnest2d_core_lib.cpp

[2026-01-29] Правка Dockerfile для совместимого healthcheck.
- HEALTHCHECK переведен на exec-форму CMD (без CMD-SHELL) для поддержки более старых версий Docker.

Файлы:
- Dockerfile

[2026-01-29] Правка pyproject.toml.
- Удален неподдерживаемый параметр tool.scikit-build.wheel.package-dir.

Файлы:
- pyproject.toml

[2026-01-29] Правки сборки libnest2d.
- GIT_TAG в FetchContent закреплен на commit 663daa69e1d7478669f714218e27681edbc96640 (master).
- В Dockerfile добавлен libarmadillo-dev для оптимизатора optimlib.

Файлы:
- CMakeLists.txt
- Dockerfile

[2026-01-29] Исправление конфигурации libnest2d.
- Переключено FetchContent на Populate + add_subdirectory, добавлен патч: export optimlibOptimizer в Libnest2DTargets.

Файлы:
- CMakeLists.txt

[2026-01-29] Правка C++ модуля libnest2d.
- Для libnest2d переключено использование BottomLeftPlacer (без NLopt), конфиг allow_rotations зависит от наличия запрета у всех items.
- Исправлен расчет bounding box через libnest2d::shapelike::boundingBox.

Файлы:
- cpp/libnest2d_core_lib.cpp

[2026-01-29] Docker runtime обновлен.
- Возвращено копирование src и PYTHONPATH в runtime-слое для загрузки Python-модуля сервиса.

Файлы:
- Dockerfile

[2026-01-29] Исправление синтаксиса Python.
- Убран лишний escape в optimizer.py при копировании item.

Файлы:
- src/libnest2d_service/optimizer.py

[2026-01-29] Попытка запуска тестов с нуля (build + docker + endpoint tests).
- Запрос пользователя: выполнять только короткие последовательные команды (без больших скриптов), чтобы не ронять WSL.
- Запуск `docker run -p 8080:8080` не удался из-за занятого порта 8080 (есть сторонние контейнеры).
- Дальнейшие команды `docker ps/run/info` периодически падают с `permission denied` к `unix:///var/run/docker.sock` / `Operation not permitted`.
- Попытка сменить Docker context (`docker context use desktop-linux`) уперлась в недоступность записи `~/.docker/config.json` (permission denied).
- Итог: все тесты (build/runtime/endpoints) сейчас не выполнены из-за проблем доступа к Docker и занятого порта.

[2026-01-29] Требование к тестам эндпоинтов.
- Для имитации внешних запросов использовать `curlimages/curl`.
- Для многострочных JSON-тел предпочтительно использовать файлы и передавать их в curl через `-d @file.json`.

[2026-01-29] Прогон тестов (build + docker + endpoints) короткими командами.
- Собран builder-образ: `libnest2d-service:builder` (no-cache).
- Собран runtime-образ: `libnest2d-service:dev` (no-cache).
- Запущен контейнер на `127.0.0.1:18080`, запросы выполнялись через `curlimages/curl` с примонтированной папкой `assets/test`.
- Эндпоинты: `/health/live`, `/health/ready`, `/version` → 200.
- `/v1/optimize`:
  - `optimize_valid.json` → 200 OK.
  - `optimize_invalid_units.json` → 422 (VALIDATION_ERROR).
  - `optimize_missing_params.json` → 422 (VALIDATION_ERROR).
  - `optimize_qty_too_big.json` → 400 (CONSTRAINT_ERROR: MAX_INSTANCES).
  - `optimize_stock_too_many.json` → 400 (CONSTRAINT_ERROR: MAX_STOCK_TYPES).
  - `optimize_trim_consumes.json` → 422 (VALIDATION_ERROR).
  - `optimize_item_too_big.json` → 422 (VALIDATION_ERROR).
- Контейнер после тестов удален.

Файлы:
- assets/test/optimize_valid.json
- assets/test/optimize_invalid_units.json
- assets/test/optimize_missing_params.json
- assets/test/optimize_qty_too_big.json
- assets/test/optimize_stock_too_many.json
- assets/test/optimize_trim_consumes.json
- assets/test/optimize_item_too_big.json
