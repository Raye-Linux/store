# Raye Store

Apps for Raye Linux in one place: Flatpak apps and system packages, with
screenshots, sizes, ratings and reviews, and a Library tab that keeps your
apps up to date. It also installs Windows programs that are known to work.

- `src/` the app (Qt 6, QML)
- `data/` the lists the Store ranks and curates with
- `windows/`, `runner/` Windows programs
- `tests/` the tests

## Build

    cmake -S . -B build -G Ninja -DBUILD_TESTING=ON
    cmake --build build

## Test

    ctest --test-dir build --output-on-failure

## License

GPL-3.0-or-later (`LICENSE`).

## Links

- Website: https://rayelinux.com
- Discord: https://discord.gg/TrxhUkabyB (questions, bug reports, news)
