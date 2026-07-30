# Windows: сборка MSIX-пакета

Клиент под Windows можно упаковать в **MSIX** (Desktop-Bridge: наш обычный статический `UaRO.exe`,
завёрнутый в пакет). Даёт чистую установку/удаление/авто-апдейт через систему.

> ⚠️ **Важно про путь установки.** MSIX ВСЕГДА ставится в системную папку
> `C:\Program Files\WindowsApps\...` — задать свой путь (например `C:\games\uaro`) в MSIX **нельзя**,
> это ограничение формата. Если нужен произвольный путь + ярлыки — это классический установщик
> (Inno Setup), а не MSIX.

## Сборка пакета

MSIX собирается **по умолчанию** вместе с обычной сборкой (опция `CLIENT_WIN_MSIX=ON`), из
«Developer Command Prompt for VS» (нужен **makeappx.exe** из Windows SDK на PATH):

```bat
cd Client
cmake --preset win-msvc
cmake --build --preset win-msvc          :: соберёт UaRO.exe И UaRO.msix
```

Для **Windows on ARM** — то же самое, пресетом `win-arm64` (арх в манифесте подставляется автоматически,
x64 или arm64):

```bat
cmake --preset win-arm64
cmake --build --preset win-arm64
```

Результат — `build/<preset>/UaRO.msix` (пока НЕподписанный). Если makeappx не на PATH (не dev-prompt),
пакет автоматически НЕ собирается (сборка exe не ломается) — тогда собери из dev-prompt вручную:
`cmake --build --preset <preset> --target msix`. Отключить упаковку: `-DCLIENT_WIN_MSIX=OFF`.

В пакет кладётся `UaRO.exe` + логотипы из `uwp/Assets/` + манифест `win/Package.appxmanifest.in`
(шрифты/переводы/шейдеры уже зашиты в exe).

## Подпись (обязательна для установки)

MSIX не поставится без подписи доверенным сертификатом, причём субъект серта должен совпадать с
Publisher в манифесте (`CN=PechSoft`). Для локального теста — самоподписанный серт:

```powershell
cd Client\win
./sign-msix.ps1 -Msix ..\build\win-msvc\UaRO.msix
```

Скрипт один раз создаёт dev-сертификат (`win/certs/uaro-dev.pfx` + публичный `.cer`) и подписывает
пакет. Чтобы поставить на этой машине, сначала доверь серт (от админа):

```powershell
Import-Certificate -FilePath Client\win\certs\uaro-dev.cer -CertStoreLocation Cert:\LocalMachine\TrustedPeople
Add-AppxPackage Client\build\win-msvc\UaRO.msix
```

Для магазина/релиза — настоящий code-signing сертификат вместо самоподписанного.

## Данные игры (нюанс MSIX)

Папка установки MSIX доступна только на ЧТЕНИЕ, а запись идёт в изолированный
`%LOCALAPPDATA%\Packages\PechSoft.uaRO_*\LocalCache`. Патчер должен качать GRF именно туда (а не рядом
с exe). Пока это открытый вопрос под MSIX; для теста можно положить data вручную в LocalCache или
запускать распакованный exe. (Для классического пути `C:\games\uaro` этой проблемы нет — там всё лежит
рядом; поэтому под кастомный путь лучше Inno Setup.)
