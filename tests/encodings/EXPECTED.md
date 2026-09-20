# Encoding test samples

Regenerate everything with `./make-samples.sh` (needs `iconv`).
`char-classes-utf8.txt`, `utf8-one-unicode.txt` and `utf8-nfd.txt` are checked
in as-is.

Open a file with `./build/notes-light tests/encodings/<file>` and compare
the status bar / *Encoding Info...* dialog with the table below.

| File | Status bar should read | Notes |
|------|------------------------|-------|
| `char-classes-utf8.txt` | `UTF-8 \| TEXT \| LF` | all five highlight colors; already UTF-8, so saving it changes nothing except the stray BOM |
| `ascii-crlf.txt` | `ASCII \| TEXT \| CRLF` | nothing gets highlighted |
| `utf8-one-unicode.txt` | `UTF-8 \| TEXT \| LF` | exactly one non-ASCII character (U+2019, Ln 7 Col 55) — one orange mark, nothing else |
| `utf8-plain.txt` | `UTF-8 \| TEXT \| LF` | accented Latin only (green) |
| `utf8-bom.txt` | `UTF-8 (BOM) → UTF-8 \| TEXT \| LF` | BOM stripped on load |
| `utf8-nfd.txt` | `UTF-8 \| TEXT \| LF \| 13 non-ASCII` | decomposed accents: 13 combining marks, highlighted **teal together with the ASCII letter they sit on** (a mark alone has zero width). *Convert to UTF-8* recomposes them into 13 precomposed letters, which then highlight green |
| `cp1250.txt` | `WINDOWS-1250 → UTF-8 \| TEXT \| LF` | must stay editable, not read-only |
| `iso-8859-2.txt` | `ISO-8859-2 → UTF-8 \| TEXT \| LF` | same text as cp1250.txt |
| `cp1251.txt` | `WINDOWS-1251 → UTF-8 \| TEXT \| LF` | Cyrillic → blue highlight |
| `iso-8859-1.txt` | `WINDOWS-1250 → UTF-8` (see below) | known ambiguity |
| `utf16le-bom.txt` | `UTF-16LE (BOM) → UTF-8 \| TEXT \| LF` | |
| `utf16be-bom.txt` | `UTF-16BE (BOM) → UTF-8 \| TEXT \| LF` | |
| `utf16le-nobom.txt` | `UTF-16LE → UTF-8 \| TEXT \| LF` | detected from the NUL byte pattern |
| `utf16be-nobom.txt` | `UTF-16BE → UTF-8 \| TEXT \| LF` | |
| `mixed-encodings.txt` | `mixed → UTF-8 \| TEXT \| LF` | two encodings in one file; all four lines must read correctly, no `PrÃ­liÅ¡` mojibake |
| `binary.dat` | `... \| BIN \| ...` | read-only, Save redirects to Save As |

## Known ambiguity

`iso-8859-1.txt` is currently detected as windows-1250. The two code pages
share most accented letters, so the same bytes decode to plausible text
either way; only a few characters differ (`ñ` → `ń`, `ï` → `ď`). Without
language statistics this cannot be resolved reliably — the file is kept in
the set precisely to show that limit.

## Checklist for the highlight features

Open `char-classes-utf8.txt`, then:

1. *Encoding Info...* — scrollable window: File / Encoding details, the
   color legend with per-class counts, and a clickable list of every
   non-ASCII character (Ln/Col, U+XXXX, byte length). Clicking a row jumps
   to that character.
2. *Highlight Non-ASCII Text* — the colors from the legend:

   | Color | Class |
   |-------|-------|
   | (none) | ASCII, U+0000-U+007F, 1 byte |
   | green `#27ae60` | accented Latin |
   | orange `#d35400` | typographic lookalike |
   | red `#c0392b` | invisible / control |
   | blue `#2980b9` | other script |
   | purple `#8e44ad` | symbol / emoji |
   | teal `#16a085` | combining mark (with its base letter) |
3. Ctrl+Shift+E repeatedly — cycles through every run.
4. *Replace Lookalikes with ASCII* — sections 3 and 4 become plain ASCII,
   sections 2, 5 and 6 are untouched; title gets a `*`.
5. *Convert to UTF-8* — drops the stray BOM in section 4, normalizes to NFC
   and reports the verification: "the whole document is now UTF-8", counts
   of ASCII vs. multi-byte characters, and what is left worth cleaning
   (invisibles, lookalikes, mixed line endings, U+FFFD).
6. Ctrl+S — status bar goes back to plain `UTF-8`.

## Two different kinds of "mixed"

These two files are easy to confuse:

* `char-classes-utf8.txt` — **one** encoding (UTF-8), mixed *character
  classes* (accents, quotes, Cyrillic, emoji). Converting and saving it
  gives back the same bytes, because there is no other encoding in it. The
  highlight colors are character categories, not encodings.
* `mixed-encodings.txt` — **two** encodings in one file (UTF-8 lines and
  windows-1250 lines). Converting and saving it visibly rewrites the
  windows-1250 bytes: 206 bytes in, 218 bytes out, `\xed\x9a\x9e` becomes
  `\xc3\xad\xc5\xa1\xc5\xbe`.

## Verifying that a save really is UTF-8 only

Open any sample, run *Convert to UTF-8*, press Ctrl+S, then in a terminal:

```
file <sample>                        # must say "UTF-8 text"
iconv -f UTF-8 -t UTF-8 <sample> >/dev/null   # must exit 0
head -c3 <sample> | od -An -tx1      # must NOT be ef bb bf (no BOM)
```

All samples in this directory have been run through that loop: after
convert + save every one of them decodes as UTF-8, carries no BOM, is NFC
normalized and contains no stray U+FEFF.

## Mixed-encoding file

`mixed-encodings.txt` is the case where one file really does hold two
encodings: lines 1 and 3 are UTF-8, lines 2 and 4 are windows-1250.

1. Open it — all four lines must be readable. If line 1 shows up as
   `PrÃ­liÅ¡ Å¾lÅ¥uÄ\u008dkÃ½`, the whole file was decoded with one code page
   and the segment-wise decoder is not working.
2. The status bar shows just `mixed` — the code pages, the byte split and
   the number of stretches are in *Encoding Info...* under **Mixed source**.
3. *Convert to UTF-8* — the report names both encodings and states that the
   file will hold one encoding after saving.
4. Ctrl+S, then `file mixed-encodings.txt` in a terminal — it must report
   `UTF-8 text`, and `iconv -f UTF-8 -t UTF-8 mixed-encodings.txt` must
   succeed with no error.
