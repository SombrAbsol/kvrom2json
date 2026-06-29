/*
 * Utility functions.
 *
 * SPDX-FileCopyrightText: 2026 SombrAbsol
 *
 * SPDX-License-Identifier: MIT
 */

#include "utils.h"

#include "parse.h"

#include <ctype.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

/*
 * Wrappers for MSVC-deprecated CRT functions.
 */
#ifdef _WIN32
static FILE *xfopen(const char *path, const char *mode)
{
    FILE *f = NULL;
    fopen_s(&f, path, mode);
    return f;
}
#else
#define xfopen(path, mode) fopen((path), (mode))
#endif

/*
 * Portable strdup, duplicates a string onto the heap.
 */
char *xstrdup(const char *s)
{
    size_t n = strlen(s) + 1;
    char *d = malloc(n);
    if (d) {
        memcpy(d, s, n);
    }
    return d;
}

/*
 * Load a file into a heap-allocated buffer.
 */
bool file_load(
    const char *path, uint8_t **out_data, size_t *out_size, long known_size)
{
    *out_data = NULL;
    *out_size = 0;

    FILE *f = xfopen(path, "rb"); // ensures no CRLF translation on Windows
    if (!f) {
        return false;
    }

    long sz;
    if (known_size > 0) {
        sz = known_size; // caller already stat'd the file, skip the seek
    } else {
        // seek to the end to determine the file size without a stat() call
        if (fseek(f, 0, SEEK_END) != 0) {
            fprintf(stderr, "file_load: cannot seek in file '%s'\n", path);
            fclose(f);
            return false;
        }
        sz = ftell(f); // returns -1 on error
        if (sz < 0) {
            fprintf(stderr,
                "file_load: cannot determine size of file '%s'\n",
                path);
            fclose(f);
            return false;
        }
        rewind(f); // reset position to the beginning before reading
    }

    uint8_t *buf = malloc((size_t)sz);
    if (!buf) {
        fprintf(stderr,
            "file_load: out of memory, cannot allocate %ld bytes for '%s'\n",
            sz,
            path);
        fclose(f);
        return false;
    }

    // verify the actual byte count read matches the expected size
    if ((long)fread(buf, 1, (size_t)sz, f) != sz) {
        fprintf(stderr,
            "file_load: read error or unexpected EOF in file '%s'\n",
            path);
        free(buf);
        fclose(f);
        return false;
    }

    fclose(f);
    *out_data = buf;
    *out_size = (size_t)sz;
    return true;
}

/*
 * Decode UTF-16-LE bytes to a heap-allocated NUL-terminated UTF-8 string.
 */
char *utf16le_to_utf8(const uint8_t *src, uint16_t byte_len)
{
    size_t chars = byte_len / 2; // each UTF-16-LE code unit is 2 bytes

    /*
     * Worst-case UTF-8 output: 3 bytes per BMP code unit (U+0800..U+FFFF).
     * +1 for the NUL terminator.
     */
    char *out = malloc(chars * 3 + 1);
    if (!out) {
        fprintf(stderr,
            "utf16le_to_utf8: out of memory, cannot allocate %zu bytes\n",
            chars * 3 + 1);
        return NULL;
    }

    size_t p = 0; // write position in the output buffer
    for (size_t i = 0; i < chars; i++) {
        // reconstruct the 16-bit code unit from two consecutive bytes
        uint16_t c = (uint16_t)(src[i * 2] | (src[i * 2 + 1] << 8));

        // encode the code unit into UTF-8
        if (c < 0x80u) {
            out[p++] = (char)c;
        } else if (c < 0x800u) {
            out[p++] = (char)(0xC0u | (c >> 6)); // top 5 bits
            out[p++] = (char)(0x80u | (c & 0x3Fu)); // bottom 6 bits
        } else {
            out[p++] = (char)(0xE0u | (c >> 12)); // top 4 bits
            out[p++] = (char)(0x80u | ((c >> 6) & 0x3Fu)); // middle 6 bits
            out[p++] = (char)(0x80u | (c & 0x3Fu)); // bottom 6 bits
        }
    }
    out[p] = '\0';

    // shrink to the actual encoded length
    char *trimmed = realloc(out, p + 1);
    return trimmed ? trimmed : out;
}

/*
 * Encode a typed KVROM key field to a heap-allocated UTF-8 string.
 */
char *key_to_string(uint16_t key_type, const uint8_t *data, uint16_t len)
{
    switch (key_type) {
    // types 0 and 1 are both plain UTF-16-LE strings
    case 0:
    case 1:
        return utf16le_to_utf8(data, len);

    case 2: {
        // 64-bit integer key, stored little-endian
        if (len < 8) {
            return xstrdup("0");
        }
        uint64_t v;
        memcpy(&v, data, 8); // memcpy avoids strict-aliasing and alignment UB
        char *s = malloc(20); // "0x" + 16 hex digits + NUL = 19 bytes max
        if (!s) {
            fprintf(stderr,
                "key_to_string: out of memory, cannot allocate integer key "
                "buffer\n");
            return NULL;
        }
        snprintf(s, 20, "0x%llX", (unsigned long long)v);
        return s;
    }

    default: {
        /*
         * Unknown key type: prefix "unknown" followed by raw hex bytes so the
         * data is not silently lost and can be inspected in the JSON output.
         */
        char *s = malloc(len * 2u + 8u); // 7 chars + 2 per byte + NUL
        if (!s) {
            fprintf(stderr,
                "key_to_string: out of memory, cannot allocate %u bytes for "
                "unknown key type %u\n",
                len * 2u + 8u,
                key_type);
            return NULL;
        }
        memcpy(s, "unknown", 7);
        char *p = s + 7;

        // lookup table avoids snprintf overhead for a two-nibble conversion
        static const char hex_lut[16] = { '0',
            '1',
            '2',
            '3',
            '4',
            '5',
            '6',
            '7',
            '8',
            '9',
            'A',
            'B',
            'C',
            'D',
            'E',
            'F' };
        for (uint16_t i = 0; i < len; i++) {
            *p++ = hex_lut[data[i] >> 4];
            *p++ = hex_lut[data[i] & 0xF];
        }
        *p = '\0';
        return s;
    }
    }
}

/*
 * Decompress a raw Deflate stream.
 */
uint8_t *decompress_raw_deflate(
    const uint8_t *src, size_t src_size, size_t *out_size, size_t expected_size)
{
    z_stream zs;
    memset(&zs, 0, sizeof(zs)); // zero all fields
    if (inflateInit2(&zs, -MAX_WBITS) != Z_OK) {
        fprintf(stderr,
            "decompress_raw_deflate: inflateInit2 failed: %s\n",
            zs.msg ? zs.msg : "unknown error");
        return NULL;
    }

    // use the caller's hint when available, otherwise fall back to a heuristic
    size_t cap = expected_size ? expected_size
                               : (src_size < 128 ? 1024 : src_size * 8);
    uint8_t *out = malloc(cap);
    if (!out) {
        fprintf(stderr,
            "decompress_raw_deflate: out of memory, cannot allocate %zu bytes "
            "for decompression output\n",
            cap);
        inflateEnd(&zs);
        return NULL;
    }

    zs.next_in = (Bytef *)src;
    zs.avail_in = (uInt)src_size;

    int ret;
    do {
        /*
         * Grow the output buffer whenever it is full,
         * then point next_out at the newly available space.
         */
        if (zs.total_out >= cap) {
            cap *= 2;
            uint8_t *tmp = realloc(out, cap);
            if (!tmp) {
                fprintf(stderr,
                    "decompress_raw_deflate: out of memory, cannot grow "
                    "decompression buffer to %zu bytes\n",
                    cap);
                free(out);
                inflateEnd(&zs);
                return NULL;
            }
            out = tmp;
        }
        zs.next_out = out + zs.total_out; // resume after already-written bytes
        zs.avail_out = (uInt)(cap - zs.total_out); // remaining capacity
        ret = inflate(&zs, Z_NO_FLUSH);
        /*
         * Z_OK: more output is available or more input is needed.
         * Z_STREAM_END: all input was consumed successfully.
         */
    } while (ret == Z_OK);

    if (ret != Z_STREAM_END) {
        // any result other than Z_STREAM_END indicates a corrupt stream
        fprintf(stderr,
            "decompress_raw_deflate: inflate failed with code %d: %s\n",
            ret,
            zs.msg ? zs.msg : "unknown error");
        inflateEnd(&zs);
        free(out);
        return NULL;
    }

    *out_size = zs.total_out;
    inflateEnd(&zs);

    // only realloc when the buffer is meaningfully oversized
    if (*out_size < cap / 2) {
        uint8_t *trimmed = realloc(out, *out_size ? *out_size : 1);
        return trimmed ? trimmed : out;
    }
    return out;
}

/*
 * Internal write buffer for json_write_entries().
 */
#define WBUF_CAP (64u * 1024u)

typedef struct {
    FILE *f; // destination file
    char buf[WBUF_CAP]; // in-memory staging buffer
    size_t pos; // number of bytes currently staged in buf
} WriteBuf;

/*
 * Flush all staged bytes to the underlying file and reset the write position.
 */
static void wbuf_flush(WriteBuf *wb)
{
    if (wb->pos) {
        fwrite(wb->buf, 1, wb->pos, wb->f);
        wb->pos = 0;
    }
}

/*
 * Stage n bytes from s into the write buffer, flushing automatically whenever
 * the buffer fills up.
 */
static void wbuf_write(WriteBuf *wb, const char *s, size_t n)
{
    while (n) {
        size_t avail = WBUF_CAP - wb->pos; // space left in buffer
        size_t chunk = n < avail ? n : avail; // copy only as much as fits
        memcpy(wb->buf + wb->pos, s, chunk);
        wb->pos += chunk;
        s += chunk;
        n -= chunk;
        if (wb->pos == WBUF_CAP) {
            wbuf_flush(wb); // buffer is full, flush before continuing
        }
    }
}

/*
 * Stage a compile-time string literal without a strlen() call.
 */
#define wbuf_putlit(wb, lit) wbuf_write((wb), (lit), sizeof(lit) - 1)

/*
 * Stage a single character.
 */
static void wbuf_putc(WriteBuf *wb, char c)
{
    wb->buf[wb->pos++] = c;
    if (wb->pos == WBUF_CAP) {
        wbuf_flush(wb);
    }
}

/*
 * JSON-escape a UTF-8 string and write it to the buffer.
 */
static void json_escape(WriteBuf *wb, const char *s)
{
    const unsigned char *p = (const unsigned char *)s;
    while (*p) {
        // accumulate a run of characters that need no escaping
        const unsigned char *start = p;
        while (*p >= 0x20u && *p != '"' && *p != '\\') {
            p++;
        }
        if (p > start) {
            wbuf_write(wb, (const char *)start, (size_t)(p - start));
        }
        if (!*p) {
            break;
        }
        // handle the single special character then advance past it
        switch (*p++) {
        case '\\':
            wbuf_putlit(wb, "\\\\");
            break;
        case '"':
            wbuf_putlit(wb, "\\\"");
            break;
        case '\b':
            wbuf_putlit(wb, "\\b");
            break;
        case '\f':
            wbuf_putlit(wb, "\\f");
            break;
        case '\n':
            wbuf_putlit(wb, "\\n");
            break;
        case '\r':
            wbuf_putlit(wb, "\\r");
            break;
        case '\t':
            wbuf_putlit(wb, "\\t");
            break;
        default: {
            // other control characters must be escaped as \uXXXX
            char esc[7];
            snprintf(esc, sizeof(esc), "\\u%04x", *(p - 1));
            wbuf_write(wb, esc, 6); // always exactly 6 characters
            break;
        }
        }
    }
}

/*
 * Sort helper that pre-caches the key's numeric suffix pointer.
 */
typedef struct {
    const KvEntry *entry; // pointer into the original entry array
    const char *suffix; // start of the trailing numeric suffix, or key's NUL
} SortEntry;

/*
 * Split a key into its non-numeric prefix and the numeric suffix that follows
 * the last underscore, if any.
 */
static void key_split(const char *key, const char **suffix_start)
{
    const char *p = key + strlen(key); // start at NUL
    // walk backwards over digits
    while (p > key && (unsigned char)*(p - 1) >= '0'
        && (unsigned char)*(p - 1) <= '9') {
        p--;
    }
    // a numeric suffix must follow an underscore, otherwise treat as no suffix
    if (p > key && *(p - 1) == '_' && *p != '\0') {
        *suffix_start = p;
    } else {
        *suffix_start = key + strlen(key); // point at NUL, no numeric suffix
    }
}

/*
 * Compare two SortEntry pointers for qsort.
 */
static int sort_entry_cmp(const void *a, const void *b)
{
    const SortEntry *ea = (const SortEntry *)a;
    const SortEntry *eb = (const SortEntry *)b;
    const char *ka = ea->entry->key ? ea->entry->key : "";
    const char *kb = eb->entry->key ? eb->entry->key : "";
    const char *sa = ea->suffix;
    const char *sb = eb->suffix;

    // compare the prefix portion case-insensitively
    size_t prefix_len_a = (size_t)(sa - ka);
    size_t prefix_len_b = (size_t)(sb - kb);
    size_t cmp_len = prefix_len_a < prefix_len_b ? prefix_len_a : prefix_len_b;

    for (size_t i = 0; i < cmp_len; i++) {
        int ca = tolower((unsigned char)ka[i]);
        int cb = tolower((unsigned char)kb[i]);
        if (ca != cb) {
            return ca - cb;
        }
    }
    // if one prefix is a strict one of the other, the shorter key sorts first
    if (prefix_len_a != prefix_len_b) {
        return (int)prefix_len_a - (int)prefix_len_b;
    }

    // if prefixes are equal, compare numeric suffixes as integers
    unsigned long na = *sa ? strtoul(sa, NULL, 10) : 0;
    unsigned long nb = *sb ? strtoul(sb, NULL, 10) : 0;
    if (na != nb) {
        return (na < nb) ? -1 : 1;
    }

    // if same prefix and numeric suffix, fall back to full strcmp for stability
    return strcmp(ka, kb);
}

/*
 * Serialize all entries currently in the global entry store to a JSON object.
 */
void json_write_entries(FILE *out, bool sort)
{
    // initialize the write buffer on the stack, no heap allocation needed
    WriteBuf wb = { .f = out, .pos = 0 };
    size_t count = entries_count();
    const KvEntry *entries = entries_data();

    // when sorting, build a SortEntry array with pre-computed suffix pointers
    SortEntry *sorted = NULL;
    if (sort && count > 0) {
        sorted = malloc(count * sizeof(SortEntry));
        if (sorted) {
            for (size_t i = 0; i < count; i++) {
                sorted[i].entry = &entries[i];
                if (entries[i].key) {
                    key_split(entries[i].key, &sorted[i].suffix);
                } else {
                    sorted[i].suffix = ""; // dead slot, will be skipped
                }
            }
            qsort(sorted, count, sizeof(SortEntry), sort_entry_cmp);
        } else {
            fprintf(stderr,
                "json_write_entries: out of memory, cannot allocate sort "
                "buffer for %zu entries\n",
                count);
        }
    }

    bool first = true;
    wbuf_putlit(&wb, "{\n");
    for (size_t i = 0; i < count; i++) {
        const KvEntry *e = sorted ? sorted[i].entry : &entries[i];
        if (!e->key) {
            continue; // skip dead slots left by empty-first-occurrence eviction
        }
        if (!first) {
            wbuf_putlit(&wb, ",\n");
        }
        first = false;
        wbuf_putlit(&wb, "  \"");
        json_escape(&wb, e->key);
        wbuf_putlit(&wb, "\": ");
        if (e->value_count <= 1) {
            // single value or empty: emit a plain JSON string
            const char *v = (e->value_count == 1) ? e->values[0] : "";
            wbuf_putc(&wb, '"');
            json_escape(&wb, v ? v : "");
            wbuf_putc(&wb, '"');
        } else {
            // multiple values: emit a JSON array so no entry is lost
            wbuf_putlit(&wb, "[");
            for (size_t j = 0; j < e->value_count; j++) {
                if (j) {
                    wbuf_putlit(&wb, ", ");
                }
                wbuf_putc(&wb, '"');
                json_escape(&wb, e->values[j] ? e->values[j] : "");
                wbuf_putc(&wb, '"');
            }
            wbuf_putc(&wb, ']');
        }
    }
    wbuf_putlit(&wb, "\n}\n");
    wbuf_flush(&wb);
    free(sorted);
}
