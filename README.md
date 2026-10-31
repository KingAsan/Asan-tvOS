# Asan · Rezka для Apple TV

**Asan 0.4.0 / build 5 готов к установке через Sideloadly.**

Графитовый фон, жемчужные акценты, лёгкие полупрозрачные панели и короткие переходы. Сохранены сворачиваемое меню, системный фокус постеров, основной жанр и рейтинг. Одинаковые качества объединены в обоих меню; запасные адреса сохранены отдельно, выбранное качество запоминается.

Пользовательское имя — **Asan**, иконка — красное пиксельное сердце. Внутренние имена `KinoHD.xcodeproj`, схемы/исполняемого файла `KinoHD` и bundle ID `dev.local.KinoHD` сохранены для обновления установленного приложения.

## Подтверждённые проверки

- [Основное build-задание](https://github.com/KingAsan/Asan-tvOS/actions/runs/41127279643/job/124308955222) — **Success**: 113 package XCTest, 0 failures; 11 Python tests, OK; Simulator BUILD SUCCEEDED; device Release arm64 ARCHIVE SUCCEEDED.
- [Финальный UI-прогон](https://github.com/KingAsan/Asan-tvOS/actions/runs/41130729493) — **Success**: 11 UITests, 0 failures/skips.
- Все 18 настоящих XCUIScreen PNG просмотрены вручную. Настоящая H.264-запись переходов проверена по последовательности исходных кадров.

Проверены раскрытие/сворачивание панели, focus, жанр/рейтинг, прокрутка 240 карточек, details/Back, озвучки/качества, оба меню без дублей, guest/settings/login, сохранённый каталог при проверке аккаунта и выход Back из AVKit-контейнера. Среда: Apple TV 1080p Simulator, tvOS 26.5, macos-26-intel. Fixtures вымышленные, только DEBUG/Simulator; настоящие Premium/CDN/видео и скорость на физической A1625 не проверялись. Частота записанного VFR-видео не является app FPS.

Это облегчённое оформление для Apple TV HD A1625. Настоящий [Liquid Glass в tvOS](https://developer.apple.com/documentation/technologyoverviews/adopting-liquid-glass) поддерживается Apple TV 4K второго поколения и новее.

## Установка

Артефакт `Asan-unsigned-IPA` находится в основном build-run 41127279643. Внутреннее имя `KinoHD-unsigned.ipa`; пользователю передан тот же файл **Asan-0.4.0-unsigned.ipa**.

Размер: **4521027 bytes**. SHA256:
`52f84f03740bb7c1e7ae967e472e0bcb23f04b6acd5e11443a691c966eb52b7d`.

Выберите новый IPA в Sideloadly, подпишите тем же Apple Account и установите поверх Asan без удаления. Пароли Apple/Rezka, сертификат, provisioning и APK в репозиторий не включены.

## Исходники и воспроизводимость

Репозиторий приватный. Шесть Base64-файлов `.transfer/` содержат проверенный source ZIP. Workflow `.github/workflows/asan-build.yml` восстанавливает дерево в `project/`, проверяет SHA256, CRC и пути, запускает исходные скрипты; разрешение — `contents:read`.

- Device Release ZIP SHA256: `618557e13261a2b55d73407ee8364e893e528b708dd88b0cfad5e030247915de`.
- Финальный UI ZIP: 2154344 bytes, SHA256 `daee692f4219ae2f17ec0fb6b7e9ef01e57a7a51e8901279d52f2e4fd6a3a93b`. Source commit `1e456f3cbc8b46e68f20a9c0b186ac7efc9b0246`, workflow commit `83aa283e2c04692f0d347e4a5dc38e424efa60de`.
- App/Sources/Tests/Package.swift/Xcode project побайтово совпадают между успешным Release и финальным UI-прогоном. Исправления повторного прогона затронули только UITests и диагностический скрипт; повторный Release намеренно пропущен.
- Полный переданный пользователю **Asan-tvOS-0.4.0-source.zip** включает окончательные Markdown-отчёты. Его контрольная сумма указана в локальном delivery JSON; 78 файлов кода/ресурсов/тестов/инструментов совпадают с финальным UI ZIP.

Внутренний VALIDATION в зафиксированном CI ZIP описывает промежуточную проверку. Этот README и полный переданный source ZIP фиксируют окончательный результат двух отдельных прогонов.
