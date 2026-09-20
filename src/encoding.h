#ifndef NOTES_ENCODING_H
#define NOTES_ENCODING_H

#include <glib.h>

/* Result of sniffing a raw byte buffer. */
typedef struct {
    char     name[64];     /* display name: "UTF-8", "ASCII", "windows-1250" */
    char     charset[32];  /* iconv source name used for conversion */
    gboolean has_bom;
    gsize    bom_len;      /* bytes to skip before converting */
    gboolean is_utf8;      /* already UTF-8 (or pure ASCII) */
    int      confidence;   /* 0..100 */

    /* Set when the file mixes UTF-8 with a legacy code page: the valid
       UTF-8 sequences are kept as they are and only the invalid runs are
       decoded with `charset`. */
    gboolean mixed;
    gsize    utf8_bytes;    /* bytes that already were valid UTF-8 */
    gsize    foreign_bytes; /* bytes decoded from `charset` */
    int      foreign_runs;  /* how many separate non-UTF-8 stretches */
} NotesEncodingInfo;

/* What kind of character we are looking at. Inside a UTF-8 document every
   character below U+0080 is a plain ASCII byte; everything else is encoded
   as 2-4 bytes, and these classes say *which* kind of non-ASCII it is. */
typedef enum {
    NOTES_CHAR_ASCII = 0,   /* U+0000..U+007F, one byte */
    NOTES_CHAR_ACCENT,      /* accented Latin letters (á, ž, ü, ...) */
    NOTES_CHAR_PUNCT,       /* typographic lookalikes: " " – — … NBSP */
    NOTES_CHAR_INVISIBLE,   /* zero-width, soft hyphen, BOM, controls */
    NOTES_CHAR_SCRIPT,      /* letters of another script (Cyrillic, Greek) */
    NOTES_CHAR_SYMBOL,      /* symbols, emoji, box drawing, math */
    NOTES_CHAR_COMBINING,   /* accent drawn onto the previous letter (NFD) */
    NOTES_CHAR_CLASS_COUNT
} NotesCharClass;

/* One run of same-class non-ASCII characters, in character offsets.
   A run of combining marks also covers the ASCII base character in front of
   it, because a zero-width mark alone cannot be highlighted visibly — so
   `nonascii` (the number of characters that really are non-ASCII) can be
   smaller than end - start. */
typedef struct {
    int start;
    int end;
    int cls;                /* NotesCharClass */
    int nonascii;
} NotesEncodingSpan;

NotesCharClass encoding_classify(gunichar c);
const char    *encoding_class_name(NotesCharClass cls);
const char    *encoding_class_color(NotesCharClass cls);
const char    *encoding_class_description(NotesCharClass cls);
const char    *encoding_class_examples(NotesCharClass cls);

/* One entry of the encoding picker (Reload with / Save As with encoding). */
typedef struct {
    const char *label;    /* shown in the dialog */
    const char *charset;  /* iconv name; "" means auto-detect */
    gboolean    bom;      /* write a BOM when saving in this encoding */
} NotesCharsetChoice;

const NotesCharsetChoice *encoding_charset_list(int *count);

/* Length of a BOM at the start of `raw` that belongs to `charset`, else 0. */
gsize encoding_bom_length(const char *raw, gsize len, const char *charset);

/* Fill `info` for a charset the user picked by hand. */
void encoding_force(const char *raw, gsize len, const char *charset,
                    NotesEncodingInfo *info);

/* Encode UTF-8 text into `charset` (optionally with a BOM). Returns newly
   allocated bytes, or NULL with `err` set. */
char *encoding_from_utf8(const char *utf8, const char *charset, gboolean bom,
                         gsize *out_len, GError **err);

/* Repair text that was decoded once with the wrong code page
   ("PrÃ­liÅ¡" -> "Príliš"). Returns newly allocated text; `fixed` gets the
   number of characters that changed and `via` the code page that did it
   (both may be NULL). Returns a copy of the input when nothing helps. */
char *encoding_fix_mojibake(const char *utf8, int *fixed, const char **via);

/* Rewrite every line ending as "LF" or "CRLF". */
char *encoding_convert_eol(const char *utf8, const char *eol, int *changed);

/* ASCII replacement for a typographic lookalike, or NULL. */
const char    *encoding_ascii_equivalent(gunichar c);

void  encoding_detect(const char *raw, gsize len, NotesEncodingInfo *out);

/* Convert raw bytes to UTF-8 according to info. Returns newly allocated
   NUL-terminated UTF-8, or NULL on failure. */
char *encoding_to_utf8(const char *raw, gsize len,
                       const NotesEncodingInfo *info, gsize *out_len);

/* Collect runs of non-ASCII characters, split by class. Returns a GArray of
   NotesEncodingSpan (free with g_array_free). */
GArray *encoding_scan_nonascii(const char *utf8, gsize len);

/* Replace typographic lookalikes with their ASCII equivalents. Returns a
   newly allocated string and the number of replaced characters. */
char *encoding_asciify_punctuation(const char *utf8, int *replaced);

/* Summary of what a UTF-8 buffer actually contains. */
typedef struct {
    gboolean valid_utf8;
    long     total_chars;
    long     ascii_chars;        /* 1-byte characters */
    long     multibyte_chars;    /* 2-4 byte characters */
    long     replacement_chars;  /* U+FFFD: bytes that could not be decoded */
    long     invisible_chars;
    long     lookalike_chars;
    int      max_bytes_per_char;
} NotesEncodingReport;

void encoding_report(const char *utf8, NotesEncodingReport *out);

/* "LF", "CRLF", "CR" or "mixed" for the raw bytes. */
const char *encoding_line_endings(const char *raw, gsize len);

#endif
