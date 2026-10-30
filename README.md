# Asan · Rezka для Apple TV

**Asan 0.3.0 / build 4 готов к установке через Sideloadly.** Это текущий Asan с красным пиксельным сердцем. Имена KinoHD.xcodeproj, схемы/исполняемого файла KinoHD и bundle ID dev.local.KinoHD оставлены внутри проекта для совместимости обновления.

Сворачиваемая системная панель tvOS18+, штатный фокус на постерах, один основной жанр и рейтинг на карточке. Постеры декодируются в фоне до600px; общий кэш, отмена скрытых запросов и ограничение параллелизма сокращают повторную работу. Файловый кэш вынесен из UI, сортировка не повторяется при движении фокуса.

## Проверка

[Финальный GitHub Actions run41104547915](https://github.com/KingAsan/Asan-tvOS/actions/runs/41104547915) — **Success**:

- 107 package XCTest, 0 failures.
- 11 Python packaging tests, OK.
- tvOS Simulator Debug BUILD SUCCEEDED; device Release arm64 ARCHIVE SUCCEEDED.
- 7 настоящих tvOS UITests, 0 failures/skips; все15 XCUIScreen PNG просмотрены вручную.

Проверены раскрытие Menu и сворачивание панели, подписи/фокус/жанр/рейтинг, каталог240 карточек, details, озвучки/качества, guest/settings/login, сохранённый каталог при проверке аккаунта, выход Back из AVKit-контейнера.

Среда: Apple TV1080p Simulator tvOS26.5, macos-26-intel. Fixtures вымышленные и только DEBUG/Simulator. Реальное видео/Premium/CDN и FPS/память на физической A1625 этими проверками не измерялись.

## Установка

Скачайте артефакт **Asan-unsigned-IPA** финального run. Внутреннее историческое имя KinoHD-unsigned.ipa; локально предоставлен тот же файл Asan-0.3.0-unsigned.ipa. Размер4498654bytes, SHA256:

0dc16142242920153044d868e25bc5c9ab58de282c6e42674cffbb39237ce492

Выберите IPA в Sideloadly, подпишите тем же Apple Account и установите поверх существующего Asan без удаления. CI не хранит пароль Apple/Rezka, сертификат или provisioning.

## Исходники и воспроизводимость

Репозиторий приватный. Из-за ограничения браузерной загрузки проверенный source ZIP передан шестью Base64-файлами в .transfer/. Workflow .github/workflows/asan-build.yml восстанавливает обычное дерево в project/, проверяет SHA256, ZIP CRC и пути, затем запускает оригинальные скрипты. Разрешение workflow — contents:read.

Проверенный source ZIP:2147982bytes, SHA256 **08e80089062f0ed1451f68523041bcd9d9235e1a207e474921f9f5d4db3c193f**. Source data commit fe27fe990e253bb12f16b6232974defa8c68baed; workflow commit d105a1209d7d3ef0dfb86e5ab91390fe887df262.

Переданный пользователю полный Asan-tvOS-0.3.0-source.zip содержит обновлённые Markdown-отчёты:2147364bytes, SHA256 **58f960e6a58b658d367100c257e57b80ccec28d1a830b7839aaaea332c55719d**. Все78 файлов кода/ресурсов/тестов/инструментов совпадают с проверенной сборкой. Внутренние отчёты в зафиксированном CI ZIP описывают промежуточное состояние; этот README фиксирует окончательный результат.
