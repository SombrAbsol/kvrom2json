/*
 * KVROM format structure.
 *
 * SPDX-FileCopyrightText: 2026 SombrAbsol
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef KVROM_H
#define KVROM_H

#include <stddef.h>
#include <stdint.h>

/*
 * KVROM file signature ("KVRF").
 */
#define KVROM_MAGIC 0x4652564Bu

/*
 * 32-bit byte offset from the start of the file used as a "pointer" inside the
 * KVROM blob.
 */
typedef struct {
    uint32_t offset;
} KvromAddress;

/*
 * File header.
 */
typedef struct {
    uint32_t magicNumber; // "KVRF"
    uint32_t fileSize; // total byte length of the file
    uint32_t version; // format version number
    uint8_t stringHashAlgorithm; // hash function used to build the hash table
    uint8_t reserved[3]; // zero-padding
    int32_t entryCount; // total number of key/value entries in the file
    int32_t hashTableSize; // bucket number in the on-disk hash table
    int32_t reserved1[2]; // unused
    KvromAddress hashTableAddress; // first bucket file offset in the hash table
    KvromAddress reserved2[3]; // unused
} KvromFileHeader;

/*
 * Per-entry header.
 */
typedef struct {
    KvromAddress nextAddress; // next entry offset in the same hash bucket chain
    int32_t keyHash; // key hash as computed by the writer
    uint16_t keyTypeAndDataCategory; // packed field
    uint8_t compressionType; // 0 = uncompressed, 1 = raw Deflate
    uint8_t reserved; // zero-padding
    int32_t dataSize; // byte length of the payload right after this header
} KvromEntryHeader;

/*
 * Decoded key/value pair held in the entry store.
 */
typedef struct {
    char *key;
    char **values; // heap array of UTF-8 value strings
    size_t value_count; // number of live elements in values[]
    size_t value_cap; // allocated capacity of values[]
} KvEntry;

#endif /* KVROM_H */
