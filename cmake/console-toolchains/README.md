# `cmake/console-toolchains/` — хуки платформенных тулчейнов консолей (заглушки)

Слот под CMake-тулчейны консолей. **Пусто до лицензии**: тулчейн-файл ссылается на
компилятор/сисрут вендорского SDK (под NDA) → в публичную часть не коммитится.

Паттерн — тот же, что мы отработали на arm-кроссе (`win-arm64` / `linux-arm-gcc-release`):
для каждой консоли будет **свой пресет + свой тулчейн-файл**, активируемые явно и НЕ
трогающие десктоп по умолчанию. Когда появится SDK, лицензированный разработчик кладёт
сюда `<vendor>.toolchain.cmake` и добавляет пресет в `CMakePresets.json`:

```jsonc
{
  "name": "console-<vendor>",
  "inherits": "win-msvc",                 // или базовый, ближайший к платформе
  "toolchainFile": "${sourceDir}/cmake/console-toolchains/<vendor>.toolchain.cmake",
  "cacheVariables": {
    "CLIENT_PREBUILT_SHADERS": "...",      // консольный рендер bgfx даёт свои шейдеры
    "CLIENT_CONSOLE_BACKEND": "<vendor>"   // подключить console/<vendor>/ вместо platform/sdl
  }
}
```

Что тулчейн-файл настраивает (на стороне лицензии):
- компилятор/линкер и сисрут SDK (аналог `linux-arm64-cross.cmake`, но вендорский);
- `RendererType` bgfx консоли (AGC/GNM для PS5, NVN для Switch, D3D12 для Xbox);
- исключение десктопного `platform/sdl` и подключение `console/<vendor>/` бэкенда;
- флаг сборки без AVX2 (уже общий гейт: второй `uaro_client-avx2` только на x86/x64).

Полный план — [../../docs/console-port-plan-km.md](../../docs/console-port-plan-km.md).
