/*
 * Utility functions.
 *
 * SPDX-FileCopyrightText: 2026 SombrAbsol
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef UTILS_H
#define UTILS_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

/*
 * Portable strdup, duplicates a string onto the heap.
 */
char *xstrdup(const char *s);

/*
 * Load a file into a heap-allocated buffer.
 */
bool file_load(
    const char *path, uint8_t **out_data, size_t *out_size, long known_size);

/*
 * Decode UTF-16-LE bytes to a heap-allocated NUL-terminated UTF-8 string.
 */
char *utf16le_to_utf8(const uint8_t *src, uint16_t byte_len);

/*
 * Encode a typed KVROM key field to a heap-allocated UTF-8 string.
 */
char *key_to_string(uint16_t key_type, const uint8_t *data, uint16_t len);

/*
 * Decompress a raw Deflate stream.
 */
uint8_t *decompress_raw_deflate(const uint8_t *src,
    size_t src_size,
    size_t *out_size,
    size_t expected_size);

/*
 * Serialize all entries currently in the global entry store to a JSON object.
 */
void json_write_entries(FILE *out, bool sort);

#endif /* UTILS_H */
