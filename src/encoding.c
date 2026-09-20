#define _GNU_SOURCE
#include "encoding.h"
#include <stdio.h>
#include <string.h>

/* Legacy single-byte candidates, ordered by preference on a tie. */
typedef struct {
    const char *charset;
    gboolean    western;   /* no Latin Extended-A letters expected */
} Candidate;

static const Candidate CANDIDATES[] = {
    {"WINDOWS-1250", FALSE},  /* Central European (sk/cz/pl/hu) */
    {"ISO-8859-2",   FALSE},
    {"WINDOWS-1252", TRUE},   /* Western European */
    {"ISO-8859-1",   TRUE},
    {"WINDOWS-1251", FALSE},  /* Cyrillic */
    {"KOI8-R",       FALSE},
    {"ISO-8859-15",  TRUE},
};

static void info_set(NotesEncodingInfo *o, const char *name, const char *charset,
                     gsize bom_len, gboolean is_utf8, int conf) {
    snprintf(o->name, sizeof(o->name), "%s", name);
    snprintf(o->charset, sizeof(o->charset), "%s", charset);
    o->bom_len    = bom_len;
    o->has_bom    = bom_len > 0;
    o->is_utf8    = is_utf8;
    o->confidence = conf;
}

/* How plausible does this decoded UTF-8 text look as human text?
   Every single-byte candidate produces the same run structure for the same
   bytes, so the decision rests on which *characters* come out. */
static long score_text(const char *utf8, gboolean western) {
    long score = 0;
    long alpha = 0, nonascii_alpha = 0, cyrillic = 0, latin_ext = 0;

    for (const char *p = utf8; *p; p = g_utf8_next_char(p)) {
        gunichar c = g_utf8_get_char(p);
        if (g_unichar_isalpha(c)) alpha++;
        if (c < 0x80) continue;                 /* ASCII: neutral */
        if (g_unichar_isalpha(c))      { score += 3; nonascii_alpha++; }
        else if (g_unichar_isspace(c)) score += 0;
        else if (g_unichar_iscntrl(c) ||
                 !g_unichar_isprint(c)) score -= 8;
        else                           score -= 2;  /* stray symbol */

        if (c >= 0x100 && c <= 0x17F)  latin_ext++;   /* Latin Extended-A */
        if (c >= 0x400 && c <= 0x4FF)  cyrillic++;    /* Cyrillic */
    }

    /* A Latin-script language leans on ASCII; text where most letters are
       non-ASCII is far more likely to be a non-Latin script. */
    if (alpha > 0 && nonascii_alpha * 2 > alpha)
        score += cyrillic * 3;
    else
        score += latin_ext * 2;

    /* Without a single Latin Extended-A letter, a Western code page is the
       better label for the same bytes. */
    if (latin_ext == 0 && western) score += 2;

    return score;
}

static char *decode_mixed(const char *raw, gsize len, const char *charset,
                          gsize *out_len);

/* Walk the buffer as UTF-8 and measure how much of it really is UTF-8.
   `multibyte` counts valid 2-4 byte sequences, `foreign` counts bytes that
   cannot be part of any valid sequence, `runs` counts the separate
   stretches of such bytes. */
static void utf8_split(const char *raw, gsize len,
                       gsize *multibyte, gsize *foreign, int *runs) {
    gsize mb = 0, bad = 0;
    int r = 0;
    gboolean in_run = FALSE;

    for (gsize i = 0; i < len; ) {
        unsigned char b = (unsigned char)raw[i];
        if (b < 0x80) { i++; in_run = FALSE; continue; }

        const char *p = raw + i;
        gsize left = len - i;
        gunichar c = g_utf8_get_char_validated(p, (gssize)left);
        if (c != (gunichar)-1 && c != (gunichar)-2) {
            gsize seq = (gsize)(g_utf8_next_char(p) - p);
            mb++;
            i += seq;
            in_run = FALSE;
        } else {
            bad++;
            if (!in_run) { r++; in_run = TRUE; }
            i++;
        }
    }
    if (multibyte) *multibyte = mb;
    if (foreign)   *foreign   = bad;
    if (runs)      *runs      = r;
}

/* Heuristic: UTF-16 without a BOM shows a dense, position-biased run of
   NUL bytes (ASCII text in UTF-16 is "A\0B\0..." or "\0A\0B..."). */
static gboolean looks_like_utf16(const char *raw, gsize len, gboolean *little_endian) {
    if (len < 16) return FALSE;
    gsize check = len < 4096 ? len : 4096;
    check &= ~(gsize)1;
    gsize even_nul = 0, odd_nul = 0;
    for (gsize i = 0; i < check; i += 2) {
        if (raw[i]     == '\0') even_nul++;
        if (raw[i + 1] == '\0') odd_nul++;
    }
    gsize pairs = check / 2;
    if (odd_nul > pairs / 2 && even_nul < pairs / 8) { *little_endian = TRUE;  return TRUE; }
    if (even_nul > pairs / 2 && odd_nul < pairs / 8) { *little_endian = FALSE; return TRUE; }
    return FALSE;
}

void encoding_detect(const char *raw, gsize len, NotesEncodingInfo *out) {
    memset(out, 0, sizeof(*out));

    const unsigned char *u = (const unsigned char *)raw;

    /* 1. Byte order marks are definitive. */
    if (len >= 4 && u[0] == 0xFF && u[1] == 0xFE && u[2] == 0x00 && u[3] == 0x00) {
        info_set(out, "UTF-32LE (BOM)", "UTF-32LE", 4, FALSE, 100); return;
    }
    if (len >= 4 && u[0] == 0x00 && u[1] == 0x00 && u[2] == 0xFE && u[3] == 0xFF) {
        info_set(out, "UTF-32BE (BOM)", "UTF-32BE", 4, FALSE, 100); return;
    }
    if (len >= 3 && u[0] == 0xEF && u[1] == 0xBB && u[2] == 0xBF) {
        info_set(out, "UTF-8 (BOM)", "UTF-8", 3, TRUE, 100); return;
    }
    if (len >= 2 && u[0] == 0xFF && u[1] == 0xFE) {
        info_set(out, "UTF-16LE (BOM)", "UTF-16LE", 2, FALSE, 100); return;
    }
    if (len >= 2 && u[0] == 0xFE && u[1] == 0xFF) {
        info_set(out, "UTF-16BE (BOM)", "UTF-16BE", 2, FALSE, 100); return;
    }

    /* 2. Pure ASCII — a strict subset of UTF-8, but worth naming. */
    gboolean ascii_only = TRUE;
    for (gsize i = 0; i < len; i++) {
        if (u[i] & 0x80) { ascii_only = FALSE; break; }
    }
    if (ascii_only && len > 0) { info_set(out, "ASCII", "UTF-8", 0, TRUE, 100); return; }
    if (len == 0)              { info_set(out, "UTF-8", "UTF-8", 0, TRUE, 100); return; }

    /* 3. Valid multi-byte UTF-8. */
    if (g_utf8_validate(raw, (gssize)len, NULL)) {
        info_set(out, "UTF-8", "UTF-8", 0, TRUE, 100); return;
    }

    /* 4. BOM-less UTF-16. */
    gboolean le = TRUE;
    if (looks_like_utf16(raw, len, &le)) {
        info_set(out, le ? "UTF-16LE" : "UTF-16BE",
                      le ? "UTF-16LE" : "UTF-16BE", 0, FALSE, 80);
        return;
    }

    /* 5. Legacy single-byte code pages: decode with each, keep the most
          text-like result. The winner is also the code page used for the
          non-UTF-8 stretches if the file turns out to be mixed. */
    long best_score = G_MINLONG;
    const char *best = NULL;
    /* 32 KB is plenty to tell code pages apart and keeps detection off the
       critical path for multi-megabyte files (each candidate is decoded and
       scored, and the mixed path decodes the sample once more per
       candidate). */
    gsize sample = len < 32768 ? len : 32768;
    for (gsize i = 0; i < G_N_ELEMENTS(CANDIDATES); i++) {
        gsize written = 0;
        char *dec = g_convert(raw, (gssize)sample, "UTF-8", CANDIDATES[i].charset,
                              NULL, &written, NULL);
        if (!dec) continue;                      /* undefined byte -> not this one */
        long s = score_text(dec, CANDIDATES[i].western);
        g_free(dec);
        if (s > best_score) { best_score = s; best = CANDIDATES[i].charset; }
    }

    if (best) {
        /* 6. Mixed file: valid UTF-8 sequences *and* bytes that cannot be
              UTF-8 at all — concatenated logs, a UTF-8 file appended to a
              windows-1250 one, a database dump. Decoding the whole thing
              with one code page would mangle the UTF-8 part into mojibake,
              so each stretch is decoded on its own. */
        gsize mb = 0, foreign = 0;
        int runs = 0;
        utf8_split(raw, len, &mb, &foreign, &runs);

        int conf = best_score > 40 ? 75 : (best_score > 0 ? 55 : 35);

        /* Binary noise routinely contains a few byte pairs that happen to
           be valid UTF-8; don't label such a file "mixed". */
        gboolean has_nul = FALSE;
        for (gsize i = 0; i < len && i < 8192; i++)
            if (raw[i] == '\0') { has_nul = TRUE; break; }

        if (mb >= 2 && foreign > 0 && !has_nul) {
            /* Score each candidate on the mixed decode: the UTF-8 parts stay
               readable either way, so the winner is decided by the foreign
               stretches while keeping their ASCII context. */
            long mixed_best = G_MINLONG;
            const char *mixed_cand = best;
            for (gsize i = 0; i < G_N_ELEMENTS(CANDIDATES); i++) {
                gsize dlen = 0;
                char *dec = decode_mixed(raw, sample, CANDIDATES[i].charset, &dlen);
                if (!dec) continue;
                long sc = score_text(dec, CANDIDATES[i].western);
                g_free(dec);
                if (sc > mixed_best) { mixed_best = sc; mixed_cand = CANDIDATES[i].charset; }
            }
            best = mixed_cand;
            conf = mixed_best > 40 ? 70 : (mixed_best > 0 ? 55 : 35);

            info_set(out, best, best, 0, FALSE, conf);
            out->mixed         = TRUE;
            out->utf8_bytes    = len - foreign;
            out->foreign_bytes = foreign;
            out->foreign_runs  = runs;
            snprintf(out->name, sizeof(out->name), "mixed: UTF-8 + %s", best);
            return;
        }

        info_set(out, best, best, 0, FALSE, conf);
        return;
    }

    /* 6. Nothing decoded cleanly — treat as Latin-1 with replacement. */
    info_set(out, "unknown (ISO-8859-1 fallback)", "ISO-8859-1", 0, FALSE, 10);
}

/* Decode a mixed file stretch by stretch: valid UTF-8 sequences are copied
   through untouched, everything else is converted from `charset`. */
static char *decode_mixed(const char *raw, gsize len, const char *charset,
                          gsize *out_len) {
    GString *out = g_string_sized_new(len + len / 4);
    GString *run = g_string_new(NULL);

    for (gsize i = 0; i < len; ) {
        unsigned char b = (unsigned char)raw[i];
        gsize seq = 0;

        if (b < 0x80) {
            seq = 1;
        } else {
            const char *p = raw + i;
            gunichar c = g_utf8_get_char_validated(p, (gssize)(len - i));
            if (c != (gunichar)-1 && c != (gunichar)-2)
                seq = (gsize)(g_utf8_next_char(p) - p);
        }

        if (seq > 0) {
            if (run->len) {          /* flush the pending foreign stretch */
                char *dec = g_convert_with_fallback(run->str, (gssize)run->len,
                                "UTF-8", charset, ".", NULL, NULL, NULL);
                if (dec) { g_string_append(out, dec); g_free(dec); }
                g_string_set_size(run, 0);
            }
            g_string_append_len(out, raw + i, (gssize)seq);
            i += seq;
        } else {
            g_string_append_c(run, raw[i]);
            i++;
        }
    }
    if (run->len) {
        char *dec = g_convert_with_fallback(run->str, (gssize)run->len,
                        "UTF-8", charset, ".", NULL, NULL, NULL);
        if (dec) { g_string_append(out, dec); g_free(dec); }
    }
    g_string_free(run, TRUE);

    if (out_len) *out_len = out->len;
    return g_string_free(out, FALSE);
}

char *encoding_to_utf8(const char *raw, gsize len,
                       const NotesEncodingInfo *info, gsize *out_len) {
    const char *src = raw + info->bom_len;
    gsize srclen = len > info->bom_len ? len - info->bom_len : 0;

    if (info->mixed)
        return decode_mixed(src, srclen, info->charset, out_len);

    if (info->is_utf8 && g_utf8_validate(src, (gssize)srclen, NULL)) {
        if (out_len) *out_len = srclen;
        return g_strndup(src, srclen);
    }

    gsize written = 0;
    char *utf8 = g_convert(src, (gssize)srclen, "UTF-8", info->charset,
                           NULL, &written, NULL);
    if (!utf8)
        utf8 = g_convert_with_fallback(src, (gssize)srclen, "UTF-8",
                                       info->charset, ".", NULL, &written, NULL);
    if (!utf8)
        utf8 = g_convert_with_fallback(src, (gssize)srclen, "UTF-8",
                                       "ISO-8859-1", ".", NULL, &written, NULL);
    if (!utf8) return NULL;
    if (out_len) *out_len = written;
    return utf8;
}


/* ------------------------------------------------- character classification */

typedef struct { gunichar c; const char *ascii; } AsciiMap;

/* Characters that *look* like ASCII but are not — the usual result of a
   word processor, a PDF copy-paste or a Windows-1250 round trip. */
static const AsciiMap ASCII_MAP[] = {
    {0x00A0, " "},   /* no-break space      */
    {0x2000, " "}, {0x2001, " "}, {0x2002, " "}, {0x2003, " "},
    {0x2004, " "}, {0x2005, " "}, {0x2006, " "}, {0x2007, " "},
    {0x2008, " "}, {0x2009, " "}, {0x200A, " "}, {0x202F, " "},
    {0x205F, " "}, {0x3000, " "},
    {0x2010, "-"}, {0x2011, "-"}, {0x2012, "-"}, {0x2013, "-"},
    {0x2014, "-"}, {0x2015, "-"}, {0x2212, "-"},
    {0x2018, "'"}, {0x2019, "'"}, {0x201A, "'"}, {0x201B, "'"},
    {0x2032, "'"}, {0x00B4, "'"}, {0x02BC, "'"},
    {0x201C, "\""}, {0x201D, "\""}, {0x201E, "\""}, {0x201F, "\""},
    {0x2033, "\""}, {0x00AB, "\""}, {0x00BB, "\""},
    {0x2026, "..."},
    {0x2022, "*"}, {0x00B7, "*"},
    {0x2044, "/"}, {0x2215, "/"},
    {0x00D7, "x"},
    {0x200B, ""},  {0x200C, ""},  {0x200D, ""},   /* zero width */
    {0x00AD, ""},                                 /* soft hyphen */
    {0xFEFF, ""},                                 /* BOM / ZWNBSP */
};

const char *encoding_ascii_equivalent(gunichar c) {
    for (gsize i = 0; i < G_N_ELEMENTS(ASCII_MAP); i++)
        if (ASCII_MAP[i].c == c) return ASCII_MAP[i].ascii;
    return NULL;
}

NotesCharClass encoding_classify(gunichar c) {
    if (c < 0x80) return NOTES_CHAR_ASCII;

    GUnicodeType t = g_unichar_type(c);
    if (t == G_UNICODE_CONTROL || t == G_UNICODE_FORMAT ||
        t == G_UNICODE_SURROGATE || t == G_UNICODE_UNASSIGNED ||
        c == 0x00AD || c == 0xFEFF || (c >= 0x200B && c <= 0x200F))
        return NOTES_CHAR_INVISIBLE;

    if (encoding_ascii_equivalent(c)) return NOTES_CHAR_PUNCT;

    if (t == G_UNICODE_NON_SPACING_MARK || t == G_UNICODE_SPACING_MARK ||
        t == G_UNICODE_ENCLOSING_MARK)
        return NOTES_CHAR_COMBINING;

    if (g_unichar_isalpha(c)) {
        /* Latin-1 Supplement, Latin Extended-A/B and the Latin part of
           Latin Extended Additional are "our" accented letters. */
        if ((c >= 0x00C0 && c <= 0x024F) || (c >= 0x1E00 && c <= 0x1EFF))
            return NOTES_CHAR_ACCENT;
        return NOTES_CHAR_SCRIPT;
    }

    switch (t) {
        case G_UNICODE_MATH_SYMBOL:
        case G_UNICODE_CURRENCY_SYMBOL:
        case G_UNICODE_MODIFIER_SYMBOL:
        case G_UNICODE_OTHER_SYMBOL:
            return NOTES_CHAR_SYMBOL;   /* emoji, arrows, box drawing */
        case G_UNICODE_CONNECT_PUNCTUATION:
        case G_UNICODE_DASH_PUNCTUATION:
        case G_UNICODE_CLOSE_PUNCTUATION:
        case G_UNICODE_FINAL_PUNCTUATION:
        case G_UNICODE_INITIAL_PUNCTUATION:
        case G_UNICODE_OTHER_PUNCTUATION:
        case G_UNICODE_OPEN_PUNCTUATION:
        case G_UNICODE_SPACE_SEPARATOR:
        case G_UNICODE_LINE_SEPARATOR:
        case G_UNICODE_PARAGRAPH_SEPARATOR:
            return NOTES_CHAR_PUNCT;
        default:
            return NOTES_CHAR_SYMBOL;
    }
}

const char *encoding_class_name(NotesCharClass cls) {
    switch (cls) {
        case NOTES_CHAR_ASCII:     return "ASCII";
        case NOTES_CHAR_ACCENT:    return "Accented Latin";
        case NOTES_CHAR_PUNCT:     return "Typographic lookalike";
        case NOTES_CHAR_INVISIBLE: return "Invisible / control";
        case NOTES_CHAR_SCRIPT:    return "Other script";
        case NOTES_CHAR_SYMBOL:    return "Symbol / emoji";
        case NOTES_CHAR_COMBINING: return "Combining mark";
        default:                   return "Unknown";
    }
}

const char *encoding_class_color(NotesCharClass cls) {
    switch (cls) {
        case NOTES_CHAR_ACCENT:    return "#27ae60";  /* green  */
        case NOTES_CHAR_PUNCT:     return "#d35400";  /* orange */
        case NOTES_CHAR_INVISIBLE: return "#c0392b";  /* red    */
        case NOTES_CHAR_SCRIPT:    return "#2980b9";  /* blue   */
        case NOTES_CHAR_SYMBOL:    return "#8e44ad";  /* purple */
        case NOTES_CHAR_COMBINING: return "#16a085";  /* teal   */
        default:                   return "#7f8c8d";
    }
}

/* ------------------------------------------------------ encoding picker */

static const NotesCharsetChoice CHARSETS[] = {
    {"Auto-detect",              "",             FALSE},
    {"UTF-8",                    "UTF-8",        FALSE},
    {"UTF-8 with BOM",           "UTF-8",        TRUE},
    {"UTF-16LE with BOM",        "UTF-16LE",     TRUE},
    {"UTF-16BE with BOM",        "UTF-16BE",     TRUE},
    {"UTF-16LE without BOM",     "UTF-16LE",     FALSE},
    {"UTF-16BE without BOM",     "UTF-16BE",     FALSE},
    {"UTF-32LE with BOM",        "UTF-32LE",     TRUE},
    {"UTF-32BE with BOM",        "UTF-32BE",     TRUE},
    {"windows-1250 (Central European)", "WINDOWS-1250", FALSE},
    {"windows-1251 (Cyrillic)",  "WINDOWS-1251", FALSE},
    {"windows-1252 (Western)",   "WINDOWS-1252", FALSE},
    {"ISO-8859-1 (Latin-1)",     "ISO-8859-1",   FALSE},
    {"ISO-8859-2 (Latin-2)",     "ISO-8859-2",   FALSE},
    {"ISO-8859-15 (Latin-9)",    "ISO-8859-15",  FALSE},
    {"KOI8-R (Cyrillic)",        "KOI8-R",       FALSE},
    {"IBM852 (DOS Central European)", "IBM852",  FALSE},
    {"US-ASCII",                 "US-ASCII",     FALSE},
};

const NotesCharsetChoice *encoding_charset_list(int *count) {
    if (count) *count = (int)G_N_ELEMENTS(CHARSETS);
    return CHARSETS;
}

gsize encoding_bom_length(const char *raw, gsize len, const char *charset) {
    const unsigned char *u = (const unsigned char *)raw;
    if (g_ascii_strcasecmp(charset, "UTF-8") == 0)
        return (len >= 3 && u[0] == 0xEF && u[1] == 0xBB && u[2] == 0xBF) ? 3 : 0;
    if (g_ascii_strcasecmp(charset, "UTF-16LE") == 0)
        return (len >= 2 && u[0] == 0xFF && u[1] == 0xFE) ? 2 : 0;
    if (g_ascii_strcasecmp(charset, "UTF-16BE") == 0)
        return (len >= 2 && u[0] == 0xFE && u[1] == 0xFF) ? 2 : 0;
    if (g_ascii_strcasecmp(charset, "UTF-32LE") == 0)
        return (len >= 4 && u[0] == 0xFF && u[1] == 0xFE && u[2] == 0 && u[3] == 0) ? 4 : 0;
    if (g_ascii_strcasecmp(charset, "UTF-32BE") == 0)
        return (len >= 4 && u[0] == 0 && u[1] == 0 && u[2] == 0xFE && u[3] == 0xFF) ? 4 : 0;
    return 0;
}

void encoding_force(const char *raw, gsize len, const char *charset,
                    NotesEncodingInfo *info) {
    memset(info, 0, sizeof(*info));
    snprintf(info->name, sizeof(info->name), "%s", charset);
    snprintf(info->charset, sizeof(info->charset), "%s", charset);
    info->bom_len    = encoding_bom_length(raw, len, charset);
    info->has_bom    = info->bom_len > 0;
    info->is_utf8    = g_ascii_strcasecmp(charset, "UTF-8") == 0;
    info->confidence = 100;
}

char *encoding_from_utf8(const char *utf8, const char *charset, gboolean bom,
                         gsize *out_len, GError **err) {
    gsize written = 0;
    char *body = g_convert(utf8, -1, charset, "UTF-8", NULL, &written, err);
    if (!body) return NULL;

    const char *bom_bytes = NULL;
    gsize bom_len = 0;
    if (bom) {
        if (g_ascii_strcasecmp(charset, "UTF-8") == 0)          { bom_bytes = "\xEF\xBB\xBF";         bom_len = 3; }
        else if (g_ascii_strcasecmp(charset, "UTF-16LE") == 0)  { bom_bytes = "\xFF\xFE";             bom_len = 2; }
        else if (g_ascii_strcasecmp(charset, "UTF-16BE") == 0)  { bom_bytes = "\xFE\xFF";             bom_len = 2; }
        else if (g_ascii_strcasecmp(charset, "UTF-32LE") == 0)  { bom_bytes = "\xFF\xFE\x00\x00";     bom_len = 4; }
        else if (g_ascii_strcasecmp(charset, "UTF-32BE") == 0)  { bom_bytes = "\x00\x00\xFE\xFF";     bom_len = 4; }
    }

    if (!bom_len) {
        if (out_len) *out_len = written;
        return body;
    }

    char *out = g_malloc(bom_len + written);
    memcpy(out, bom_bytes, bom_len);
    memcpy(out + bom_len, body, written);
    g_free(body);
    if (out_len) *out_len = bom_len + written;
    return out;
}

/* ---------------------------------------------------------- mojibake fix */

/* Text that was already UTF-8 but got decoded once as a single-byte code
   page reads as "PrÃ­liÅ¡": the original UTF-8 bytes survive, each one
   turned into its own character. Encoding the text back into that code
   page recovers the bytes, and if they are valid UTF-8 the damage is
   undone. */
static long count_nonascii(const char *utf8) {
    long n = 0;
    for (const char *p = utf8; *p; p = g_utf8_next_char(p))
        if (g_utf8_get_char(p) >= 0x80) n++;
    return n;
}

static gboolean has_broken_chars(const char *utf8) {
    for (const char *p = utf8; *p; p = g_utf8_next_char(p)) {
        gunichar c = g_utf8_get_char(p);
        if (c < 0x80) continue;
        GUnicodeType t = g_unichar_type(c);
        if (t == G_UNICODE_UNASSIGNED || t == G_UNICODE_SURROGATE ||
            t == G_UNICODE_CONTROL || c == 0xFFFD)
            return TRUE;
    }
    return FALSE;
}

char *encoding_fix_mojibake(const char *utf8, int *fixed, const char **via) {
    static const char *const SUSPECTS[] = {
        "WINDOWS-1252", "WINDOWS-1250", "ISO-8859-1", "WINDOWS-1251", "ISO-8859-2",
    };

    if (fixed) *fixed = 0;
    if (via)   *via   = NULL;
    if (!utf8) return NULL;

    /* Mojibake always inflates: one real character became two or three
       stray ones. A repair is therefore recognised by the non-ASCII count
       going *down* while the result stays valid, sane UTF-8. */
    long before = count_nonascii(utf8);
    if (before == 0) return g_strdup(utf8);   /* pure ASCII, nothing to undo */

    char *best = NULL;
    const char *best_cs = NULL;
    long best_gain = 0;

    for (gsize i = 0; i < G_N_ELEMENTS(SUSPECTS); i++) {
        gsize n = 0;
        char *bytes = g_convert(utf8, -1, SUSPECTS[i], "UTF-8", NULL, &n, NULL);
        if (!bytes) continue;          /* text holds characters that code page lacks */

        gboolean usable = FALSE;
        if (g_utf8_validate(bytes, (gssize)n, NULL)) {
            gsize mb = 0, foreign = 0;
            utf8_split(bytes, n, &mb, &foreign, NULL);
            if (mb > 0 && foreign == 0 && !has_broken_chars(bytes)) {
                long after = count_nonascii(bytes);
                long gain = before - after;
                if (gain > best_gain) {
                    g_free(best);
                    best = g_strndup(bytes, n);
                    best_gain = gain;
                    best_cs = SUSPECTS[i];
                    usable = TRUE;
                }
            }
        }
        (void)usable;
        g_free(bytes);
    }

    if (!best) return g_strdup(utf8);

    if (via)   *via = best_cs;
    if (fixed) *fixed = (int)best_gain;
    return best;
}

/* -------------------------------------------------------- line endings */

char *encoding_convert_eol(const char *utf8, const char *eol, int *changed) {
    gboolean want_crlf = g_ascii_strcasecmp(eol, "CRLF") == 0;
    GString *out = g_string_sized_new(strlen(utf8) + 16);
    int n = 0;

    for (const char *p = utf8; *p; p++) {
        if (*p == '\r') {
            gboolean had_crlf = (p[1] == '\n');
            if (had_crlf) p++;
            if (want_crlf) g_string_append(out, "\r\n");
            else           g_string_append_c(out, '\n');
            if (want_crlf != had_crlf) n++;
        } else if (*p == '\n') {
            if (want_crlf) { g_string_append(out, "\r\n"); n++; }
            else           g_string_append_c(out, '\n');
        } else {
            g_string_append_c(out, *p);
        }
    }

    if (changed) *changed = n;
    return g_string_free(out, FALSE);
}

const char *encoding_class_description(NotesCharClass cls) {
    switch (cls) {
        case NOTES_CHAR_ASCII:
            return "U+0000-U+007F, one byte per character";
        case NOTES_CHAR_ACCENT:
            return "Accented Latin letters, 2 bytes (U+00C0-U+024F, U+1E00-U+1EFF)";
        case NOTES_CHAR_PUNCT:
            return "Punctuation that looks like ASCII but is not - has an ASCII equivalent";
        case NOTES_CHAR_INVISIBLE:
            return "Zero-width, soft hyphen, BOM and control characters - invisible in the text";
        case NOTES_CHAR_SCRIPT:
            return "Letters of a non-Latin script; homoglyphs can imitate Latin letters";
        case NOTES_CHAR_SYMBOL:
            return "Symbols, emoji, math, arrows and box drawing";
        case NOTES_CHAR_COMBINING:
            return "Accent drawn onto the letter before it (decomposed, NFD) - "
                   "zero width on its own, so the base letter is highlighted "
                   "with it. Convert to UTF-8 recomposes these into single "
                   "characters";
        default:
            return "";
    }
}

const char *encoding_class_examples(NotesCharClass cls) {
    switch (cls) {
        case NOTES_CHAR_ASCII:     return "A b 7 , ( ) space";
        case NOTES_CHAR_ACCENT:    return "\xc3\xa1 \xc5\xbe \xc3\xb4 \xc5\x88 \xc3\xbc \xc3\x9f";
        case NOTES_CHAR_PUNCT:     return "\xe2\x80\x9c \xe2\x80\x9d \xe2\x80\x93 \xe2\x80\x94 \xe2\x80\xa6 NBSP";
        case NOTES_CHAR_INVISIBLE: return "U+200B U+00AD U+FEFF";
        case NOTES_CHAR_SCRIPT:    return "\xd0\x9f \xd1\x80 \xce\x9a \xe4\xb8\x96";
        case NOTES_CHAR_SYMBOL:    return "\xe2\x9c\x94 \xe2\x86\x92 \xe2\x88\x91 \xe2\x82\xac";
        case NOTES_CHAR_COMBINING: return "U+0301 U+030C U+0327";
        default:                   return "";
    }
}

char *encoding_asciify_punctuation(const char *utf8, int *replaced) {
    GString *out = g_string_sized_new(strlen(utf8) + 16);
    int n = 0;
    for (const char *p = utf8; *p; p = g_utf8_next_char(p)) {
        gunichar c = g_utf8_get_char(p);
        const char *rep = c < 0x80 ? NULL : encoding_ascii_equivalent(c);
        if (rep) {
            g_string_append(out, rep);
            n++;
        } else {
            g_string_append_unichar(out, c);
        }
    }
    if (replaced) *replaced = n;
    return g_string_free(out, FALSE);
}

GArray *encoding_scan_nonascii(const char *utf8, gsize len) {
    GArray *spans = g_array_new(FALSE, FALSE, sizeof(NotesEncodingSpan));
    const char *end = utf8 + len;
    int index = 0;
    int prev_ascii = -1;     /* index of the previous character, if ASCII */
    NotesEncodingSpan cur = {-1, -1, NOTES_CHAR_ASCII, 0};

    for (const char *p = utf8; p < end && *p; p = g_utf8_next_char(p), index++) {
        gunichar c = g_utf8_get_char(p);
        NotesCharClass cls = encoding_classify(c);

        if (cls == NOTES_CHAR_ASCII) {
            if (cur.start >= 0) { g_array_append_val(spans, cur); cur.start = -1; }
            prev_ascii = index;
            continue;
        }
        /* Break the run whenever the class changes, so each run can be
           painted in its own color. */
        if (cur.start >= 0 && cur.cls != (int)cls) {
            g_array_append_val(spans, cur);
            cur.start = -1;
        }
        if (cur.start < 0) {
            cur.start = index;
            cur.cls = (int)cls;
            cur.nonascii = 0;
            /* A combining mark has no width of its own: highlighting only
               the mark paints nothing the eye can find. Pull the ASCII base
               character into the run so the whole glyph lights up. */
            if (cls == NOTES_CHAR_COMBINING && prev_ascii == index - 1)
                cur.start = prev_ascii;
        }
        cur.nonascii++;
        cur.end = index + 1;
        prev_ascii = -1;
    }
    if (cur.start >= 0) g_array_append_val(spans, cur);
    return spans;
}

void encoding_report(const char *utf8, NotesEncodingReport *out) {
    memset(out, 0, sizeof(*out));
    out->valid_utf8 = g_utf8_validate(utf8, -1, NULL);

    for (const char *p = utf8; *p; p = g_utf8_next_char(p)) {
        gunichar c = g_utf8_get_char(p);
        int bytes = (int)(g_utf8_next_char(p) - p);

        out->total_chars++;
        if (c < 0x80) out->ascii_chars++;
        else          out->multibyte_chars++;
        if (bytes > out->max_bytes_per_char) out->max_bytes_per_char = bytes;

        if (c == 0xFFFD) out->replacement_chars++;

        switch (encoding_classify(c)) {
            case NOTES_CHAR_INVISIBLE: out->invisible_chars++;  break;
            case NOTES_CHAR_PUNCT:     out->lookalike_chars++;  break;
            default: break;
        }
    }
}

const char *encoding_line_endings(const char *raw, gsize len) {
    gboolean crlf = FALSE, lf = FALSE, cr = FALSE;
    /* A full pass over a multi-megabyte buffer costs more than the answer is
       worth: line endings are uniform in practice, and a file that switches
       style does so long before this. */
    if (len > 1024 * 1024) len = 1024 * 1024;
    for (gsize i = 0; i < len; i++) {
        if (raw[i] == '\0') continue;   /* UTF-16/32 padding */
        if (raw[i] == '\r') {
            gsize j = i + 1;
            while (j < len && raw[j] == '\0') j++;
            if (j < len && raw[j] == '\n') { crlf = TRUE; i = j; }
            else cr = TRUE;
        } else if (raw[i] == '\n') {
            lf = TRUE;
        }
    }
    int kinds = (crlf ? 1 : 0) + (lf ? 1 : 0) + (cr ? 1 : 0);
    if (kinds > 1) return "mixed";
    if (crlf) return "CRLF";
    if (cr)   return "CR";
    return "LF";
}
