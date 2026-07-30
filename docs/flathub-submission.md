# Публикация BornRok на Flathub

Flathub — публичный магазин Flatpak. После публикации любой пользователь ставит клиент командой
`flatpak install flathub com.bornrok.BornRok`, обновления — `flatpak update`.

## Ключевой момент: исходники приватные → модель `extra-data`

Flathub собирает приложения на СВО�ей инфраструктуре из манифеста в PR и не видит твой локальный
бинарь. Собрать из исходников нельзя (репозиторий закрыт). Поэтому используется **extra-data**: бинарь
клиента качается при УСТАНОВКЕ с `bornrok.com` (так на Flathub живут Spotify/Discord/Zoom). Flathub
раздаёт только крошечный манифест + метаданные; сам бинарь отдаёт твой сервер.

Готовый шаблон манифеста: `packaging/flatpak/flathub/com.bornrok.BornRok.yml` (осталось подставить
URL/sha256/size — см. ниже).

## Что нужно сделать (твоя часть)

1. **Домен.** app-id `com.bornrok.BornRok` завязан на `bornrok.com` — Flathub потребует подтвердить
   владение доменом (обычно достаточно, что сайт на нём живой; иногда просят DNS-TXT/файл-подтверждение).

2. **Залить бинари на стабильные https-URL** (по одному на арку):
   - собрать: `./scripts/build-linux.sh` (даст x86_64 и aarch64 бинари);
   - выложить, например, как `https://bornrok.com/flatpak/bin/bornrok-x86_64` и `…-aarch64`;
   - посчитать хэш и размер каждого:
     ```
     sha256sum bornrok-x86_64        # -> sha256
     stat -c%s  bornrok-x86_64        # -> size (байты)
     ```
   - вписать `url` / `sha256` / `size` в оба блока `extra-data` манифеста (сейчас там TODO-заглушки).
   - **важно:** при каждом обновлении клиента бинарь на URL меняется → надо обновлять sha256/size
     отдельным PR в Flathub (extra-data привязан к хэшу). Либо версионировать URL.

3. **Скриншоты.** Flathub требует минимум один скриншот по публичному https-URL. Залей 1–3 PNG/JPG
   (16:9, без рамок окна) на `https://bornrok.com/flatpak/screenshots/…` и поправь ссылки в
   `packaging/flatpak/com.bornrok.BornRok.metainfo.xml` (сейчас там плейсхолдеры).

4. **Content rating.** В метаданных проставлен OARS: mild fantasy violence + online chat. Проверь, что
   соответствует реальному контенту, поправь при необходимости.

5. **Сабмишен PR.**
   - форкни `github.com/flathub/flathub`;
   - создай ветку `com.bornrok.BornRok`, положи в неё: манифест
     `com.bornrok.BornRok.yml` (из `packaging/flatpak/flathub/`), а также `com.bornrok.BornRok.desktop`,
     `com.bornrok.BornRok.metainfo.xml`, `icon.png`, `icon-128.png`;
   - открой PR. Ревьюеры пройдутся по манифесту/метаданным, помогут поправить мелочи extra-data;
   - после мержа создаётся репозиторий `flathub/com.bornrok.BornRok`, приложение уходит в магазин.

## Проверка перед PR (локально)

- Валидность метаданных: `flatpak run org.freedesktop.appstream-glib validate packaging/flatpak/com.bornrok.BornRok.metainfo.xml`
  (или `appstreamcli validate …`).
- Тестовая сборка extra-data-манифеста локально:
  `flatpak-builder --user --force-clean --install-deps-from=flathub build/flathub-test packaging/flatpak/flathub/com.bornrok.BornRok.yml`
  (после того как заполнишь sha256/size — иначе упадёт на проверке хэша).

## Оговорка

Flathub придирчив к «тонким лаунчерам». У нас flatpak — это САМ клиент (рендерит и играет, контент
докачивает встроенный патчер), так что кейс проходит, но ревью проприетарного extra-data-приложения
идёт строже и дольше обычного. Быстрый и полностью подконтрольный вариант без ревью — свой Flatpak-репо
на bornrok.com (см. обсуждение в задаче), Flathub — ради витрины и авто-обнаружения.
