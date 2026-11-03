# Quran for Pebble

A Quran reader for the Pebble Time 2 (`emery`). It works on its own, and uses the phone to render pages whenever the phone is connected. The rendering engine is generic, so other screens (Round 2, Pebble 2) only need a profile and a line table.

## How it works

- **Lines are the unit.** Each display line (at a given text size and line width) is rendered as a compressed 2-bit image strip. The watch caches strips and stacks them on screen, so page turns are instant once the lines are cached. It prefetches ahead and behind, sized to fit its cache.
- **Phone first, watch as fallback.** On every page change the watch checks whether the phone is connected and its PebbleKit JS has answered with the same data version. If so, the phone renders. If not, or if the phone is silent for 2.5 s, the watch renders the lines itself. Both run the same algorithm and produce byte-identical strips, so one page can mix both sources.
- **Two Arabic fonts.** By default the bundled Quran glyphs are used. With "Arabic text: System font" (needs the phone, and a language pack with Arabic glyphs such as "English + Arabic glyphs"), the reader shows the Tanzil *Quran Simple* text in the Pebble system font. The watch breaks those lines itself, because the firmware cuts right-to-left lines after about 32 characters. Without the phone it falls back to the bundled glyphs.
- **Surah headers.** Every surah opens with a decorated rule carrying its name, then the basmala.
- **Precomputed layout.** `LINES.bin` holds the number of display lines of every ayah at every text size, for one line width, so navigating never lays out text.
- **Menu-first launch and crash recovery.** The app always opens on the menu. If the previous run did not exit cleanly, settings are reset to defaults (reading position and bookmarks are kept) and a notice is shown.
- **UI languages.** Menus, dialogs and the settings page are in English, French, Arabic, Turkish, German, Dutch, Spanish or Indonesian (see [Languages](#languages)).
- **Phone services.** Translations come from the free `fawazahmed0/quran-api` (cached on the phone). The settings page is a `data:` URL, so nothing needs hosting. The app declares manifest version 2 with a single permission: internet access to the two text hosts.

Persisted watch data stays far below the 4 KB per-app limit: settings, position, bookmarks, the crash flag and the UI language (its own key, so 2.0 data loads unchanged).

## Languages

"Language" in the settings page (and in the watch's Settings) picks the language of the watch menus and of the settings page. *Automatic* follows the watch's system language on the watch and the phone's language on the page. The watch shows Arabic only when its system font has Arabic glyphs (a language pack such as "English + Arabic glyphs"); otherwise it uses English. In Arabic, surah names are shown in Arabic script. Arabic UI strings are kept short because the firmware cuts right-to-left lines after about 32 characters; long Arabic texts carry their own line breaks.

All texts live in `lang/<code>.json` (`watch`: watch strings, `page`: settings page). The build runs `tools/gen_lang.py`, which writes the `LANG.bin` resource (the watch loads only the language in use), `src/c/i18n_ids.h` (the `S_*` ids used with `_()`) and `src/pkjs/i18n_data.js`. English is the reference.

To add a language:
1. Copy `lang/en.json` to `lang/<code>.json` (two-letter code, as in the locale `xx_YY`) and translate `name` (in that language), then every string. Keep the `%d`/`%s` conversions and leading spaces.
2. Append the code to `LANGS` in `tools/gen_lang.py` (append only: the position is what the watch saves).
3. Run `pebble build`, then `node tools/host/test_lang.js`.

To add a string, add it to `lang/en.json` and every other language, then use `_(S_<KEY>)` in the watch code or `'@<key>'`/`data-t` in `src/pkjs/config.js`.

## Platforms

Everything screen-specific lives in `src/c/profile.c`: the text column (x, width) and, for round screens, the rows where the whole column is visible. The engine takes the line width at runtime (`qr_set_width`), and the phone renders at the width the watch sends with each request.

To add a platform:
1. Add it to `targetPlatforms`.
2. Generate a line table for its column width: `qtool lines resources/data resources/data/LINES~<platform>.bin <width>`. Resource tags make the SDK pick the right file per platform.
3. Check memory and the frame buffer format. The compositor currently draws into 8-bit colour frame buffers; Pebble 2 (1-bit) would need a 1-bit path in `strips.c`.

## Layout

| Path | What |
|---|---|
| `src/c/qdata.c` | Data access: metadata, Huffman text/lexicon decoding, glyph tables and caches |
| `src/c/qrender.c` | Width-independent layout and strip rendering, strip codec |
| `src/c/profile.c` | Per-platform screen profile |
| `src/c/source.c` | Phone or watch rendering decision, requests, prefetch, memory budget |
| `src/c/strips.c` | Strip cache and frame-buffer compositing (themes) |
| `src/c/systext.c` | System-font reading: text cache, line breaking, drawing |
| `src/c/reader.c`, `textview.c`, `picker.c`, `menus.c` | Windows |
| `src/c/comm.c` | AppMessage protocol (commands in `comm.h`) |
| `src/pkjs/index.js` | Phone side: protocol, text, settings sync |
| `src/pkjs/render.js` | JS port of the renderer (checked byte for byte against the C one) |
| `src/pkjs/arabic.js` | Decoder for the bundled Arabic text |
| `src/pkjs/config.js` | Settings page |
| `lang/*.json`, `src/c/i18n.c` | UI languages (see above); `LANG.bin`, `i18n_ids.h`, `i18n_data.js` are made by `tools/gen_lang.py` during the build |
| `src/pkjs/data_blob.js`, `src/c/data_version.h` | Generated by `tools/gen_js_data.py` during the build |
| `src/pkjs/arabic_blob.js`, `src/c/names_ar.c` | Generated by `tools/gen_arabic.py` (see its header for the inputs) |
| `tools/host/` | Host build of the renderer and tests |

## Build

```sh
pebble build
pebble install --phone <phone-ip>      # or --emulator emery
```

## Tools and tests

```sh
# host build of the watch renderer
gcc -O2 -DHOST_BUILD -Isrc/c -o qtool tools/host/qtool.c src/c/qdata.c src/c/qrender.c

./qtool lines resources/data resources/data/LINES.bin 200   # line counts for a 200 px column
./qtool names resources/data resources/data/NAMES.bin       # surah names for the menu
./qtool stats resources/data                                # strip sizes, cache stats

# the JS renderer must match C byte for byte (any width)
./qtool strips resources/data 3 0 6236 /tmp/ref.bin 200
node tools/host/test_render.js /tmp/ref.bin 3 0 6236 200 resources/data/LINES.bin

# UI translations: every language has every string, Arabic lines stay short,
# generated files are up to date, the settings page builds in every language
node tools/host/test_lang.js
```

If the data files change, regenerate `LINES.bin` and `NAMES.bin`. The build then regenerates the JS blob and data version.
