# Native C Demo

A native Windows `.exe` version of the Zealand Byte Division cracker intro, built with **C + SDL2** — no browser, no JavaScript, no runtime dependencies.

## Build

Requires MSYS2 with `mingw-w64-x86_64-gcc` and `mingw-w64-x86_64-SDL2`:

```bash
cd native
make
```

The output is `zealand-demo.exe`. Copy `SDL2.dll` from `C:\msys64\mingw64\bin\` next to the exe if distributing.

## Run

```bash
make run
# or double-click zealand-demo.exe
```

Must be run from the `native/` folder (MOD files are loaded from `../mods/`).

## Keys

| Key | Action |
|-----|--------|
| N | Next MOD track |
| P | Previous MOD track |
| M | Mute / unmute |
| F | Toggle fullscreen |
| ESC | Quit |

## How it works

| Layer | Technology |
|-------|-----------|
| Window & input | SDL2 |
| Audio mixing | SDL2 AudioDevice + hand-written ProTracker MOD mixer |
| Graphics | Software renderer — every pixel drawn by hand into a `uint32_t` buffer |
| Beat detection | RMS energy from the MOD mixer output |
| Font | 5×7 bitmap pixel font in `font.h` |
