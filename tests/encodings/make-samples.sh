#!/bin/sh
# Regenerates the encoding test samples next to this script.
# char-classes-utf8.txt and utf8-nfd.txt are checked in; every other sample
# is generated here with iconv.
set -e
cd "$(dirname "$0")"

# ---- pure ASCII, CRLF line endings ------------------------------------
printf 'Plain ASCII only.\r\nNo byte above 0x7F on any line.\r\nStatus bar should read: ASCII | TEXT | CRLF\r\n' > ascii-crlf.txt

# ---- Central European text, three encodings of the same content -------
printf 'Príliš žlťučký kôň úpel ďábelské ódy.\nŽivot je krásny, ale kôň je rýchlejší.\n' > /tmp/ce-sample.$$
iconv -f UTF-8 -t WINDOWS-1250 /tmp/ce-sample.$$ > cp1250.txt
iconv -f UTF-8 -t ISO-8859-2   /tmp/ce-sample.$$ > iso-8859-2.txt
cp /tmp/ce-sample.$$ utf8-plain.txt

# ---- UTF-8 with BOM ---------------------------------------------------
printf '\357\273\277' > utf8-bom.txt
cat /tmp/ce-sample.$$ >> utf8-bom.txt

# ---- UTF-16, with and without BOM ------------------------------------
iconv -f UTF-8 -t UTF-16LE /tmp/ce-sample.$$ > utf16le-nobom.txt
iconv -f UTF-8 -t UTF-16BE /tmp/ce-sample.$$ > utf16be-nobom.txt
printf '\377\376' > utf16le-bom.txt
cat utf16le-nobom.txt >> utf16le-bom.txt
printf '\376\377' > utf16be-bom.txt
cat utf16be-nobom.txt >> utf16be-bom.txt
rm -f /tmp/ce-sample.$$

# ---- Cyrillic in windows-1251 ----------------------------------------
printf 'Привет мир, как дела сегодня вечером?\nЭто тестовый файл в кодировке windows-1251.\n' \
  | iconv -f UTF-8 -t WINDOWS-1251 > cp1251.txt

# ---- Western European in ISO-8859-1 ----------------------------------
printf 'Grüße aus München, schöne Grüße für alle.\nCafé, naïve, façade, jalapeño.\n' \
  | iconv -f UTF-8 -t ISO-8859-1 > iso-8859-1.txt

# ---- UTF-8 with exactly one non-ASCII character ----------------------
# (utf8-one-unicode.txt is checked in; this only verifies it stayed intact)
if [ -f utf8-one-unicode.txt ]; then
  n=$(LC_ALL=C tr -d '\000-\177' < utf8-one-unicode.txt | wc -c)
  [ "$n" = "3" ] || echo "WARNING: utf8-one-unicode.txt should hold exactly one 3-byte non-ASCII character, got $n bytes"
fi

# ---- genuinely mixed file: UTF-8 and windows-1250 bytes side by side --
{ printf 'Riadok 1 je v UTF-8: Príliš žlťučký kôň.\n'
  printf 'Riadok 2 je vo windows-1250: '
  printf 'Príliš žlťučký kôň.\n' | iconv -f UTF-8 -t WINDOWS-1250
  printf 'Riadok 3 je zase UTF-8: ďábelské ódy \360\237\216\211\n'
  printf 'Riadok 4 vo windows-1250: '
  printf 'meniny má Žofia, počasie je krásne.\n' | iconv -f UTF-8 -t WINDOWS-1250
} > mixed-encodings.txt

# ---- binary (not text at all) ----------------------------------------
head -c 4096 /bin/true > binary.dat

echo "samples regenerated in $(pwd)"
