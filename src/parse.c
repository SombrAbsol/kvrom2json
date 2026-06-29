/*
 * Parse KVROM data.
 *
 * SPDX-FileCopyrightText: 2026 SombrAbsol
 *
 * SPDX-License-Identifier: MIT
 */

#include "parse.h"

#include "utils.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define HT_EMPTY   SIZE_MAX
#define HT_DELETED (SIZE_MAX - 1u)

/*
 * Initial hash-table capacity.
 */
#define HT_MIN_CAP 512u

static KvEntry *g_entries = NULL;
static size_t g_count = 0;
static size_t g_capacity = 0;

static size_t *g_ht = NULL;
static size_t g_ht_cap = 0;
static size_t g_ht_used = 0; // live slots + tombstone (HT_DELETED) slots

/*
 * FNV-1a 32-bit hash of a NUL-terminated string.
 */
static uint32_t fnv1a(const char *s)
{
    uint32_t h = 2166136261u; // FNV offset basis
    while (*s) {
        h ^= (uint8_t)*s++; // XOR one byte at a time
        h *= 16777619u; // FNV prime
    }
    return h;
}

/*
 * Forward decl needed by ht_grow.
 */
static void ht_insert_index(size_t idx, uint32_t hash);

/*
 * Double the hash table capacity and rehash all live entries.
 */
static void ht_grow(void)
{
    size_t new_cap = g_ht_cap ? g_ht_cap * 2 : HT_MIN_CAP;
    size_t *old_ht = g_ht;
    size_t old_cap = g_ht_cap;

    g_ht = malloc(new_cap * sizeof(size_t));
    if (!g_ht) {
        // restore previous state so the store stays usable
        g_ht = old_ht;
        g_ht_cap = old_cap;
        fprintf(stderr,
            "ht_insert_index: out of memory, cannot grow hash table to %zu "
            "slots\n",
            new_cap);
        return;
    }
    g_ht_cap = new_cap; // update capacity now that the allocation succeeded
    g_ht_used = 0; // reset before reinserting so tombstones are not counted

    // SIZE_MAX has all bits set, initialize every slot to HT_EMPTY
    memset(g_ht, 0xFF, new_cap * sizeof(size_t));

    // reinsert only live entries, tombstones are intentionally skipped
    for (size_t i = 0; i < old_cap; i++) {
        if (old_ht[i] != HT_EMPTY && old_ht[i] != HT_DELETED) {
            uint32_t h = fnv1a(g_entries[old_ht[i]].key);
            ht_insert_index(old_ht[i], h);
        }
    }
    free(old_ht);
}

/*
 * Insert an existing g_entries index into the hash table without checking for
 * duplicates.
 */
static void ht_insert_index(size_t idx, uint32_t hash)
{
    size_t slot
        = hash & (g_ht_cap - 1); // fast modulo because cap is a power of 2
    for (;;) {
        size_t v = g_ht[slot];
        if (v == HT_EMPTY || v == HT_DELETED) {
            g_ht[slot] = idx;
            g_ht_used++;
            return;
        }
        // collision, advance to the next slot, wrapping around
        slot = (slot + 1) & (g_ht_cap - 1);
    }
}

/*
 * Search the hash table for 'key'.
 */
static size_t ht_find(const char *key, uint32_t hash, size_t *insert_slot)
{
    if (!g_ht_cap) {
        // hash table not yet allocated, nothing can be found
        if (insert_slot) {
            *insert_slot = HT_EMPTY;
        }
        return HT_EMPTY;
    }

    size_t slot = hash & (g_ht_cap - 1);
    size_t first_del = HT_EMPTY; // first tombstone encountered during probe

    for (;;) {
        size_t v = g_ht[slot];

        if (v == HT_EMPTY) {
            // probe terminated, key is definitely not in the table
            if (insert_slot) {
                // prefer the tombstone slot if one was seen
                *insert_slot = (first_del != HT_EMPTY) ? first_del : slot;
            }
            return HT_EMPTY;
        }

        if (v == HT_DELETED) {
            // record the first tombstone but keep probing
            if (first_del == HT_EMPTY) {
                first_del = slot;
            }
        } else if (strcmp(g_entries[v].key, key) == 0) {
            // key found, return its slot
            if (insert_slot) {
                *insert_slot = slot;
            }
            return slot;
        }

        slot = (slot + 1) & (g_ht_cap - 1);
    }
}

/*
 * Add a key/value pair to the global store.
 */
void entries_add(char *key, char *value)
{
    // grow when the combined count of live + tombstone slots exceeds 65%
    if (g_ht_used * 100 >= (g_ht_cap ? g_ht_cap : 0) * 65) {
        ht_grow();
    }

    // compute the hash once; it is reused by both ht_find and ht_insert_index
    uint32_t hash = fnv1a(key);

    size_t insert_slot;
    size_t found = ht_find(key, hash, &insert_slot);

    if (found != HT_EMPTY) {
        // key already exists: append the new value if it is non-empty
        size_t idx = g_ht[found];
        KvEntry *e = &g_entries[idx];

        int new_empty = !value || value[0] == '\0';
        if (new_empty) {
            free(value);
            free(key);
            return;
        }

        // check whether the sole existing value is an empty placeholder
        int old_empty
            = e->value_count == 1 && (!e->values[0] || e->values[0][0] == '\0');
        if (old_empty) {
            // first occurrence was empty, discard it
            free(e->values[0]);
            free(e->values);
            free(e->key);
            e->key = NULL; // mark the g_entries slot as dead
            e->values = NULL;
            e->value_count = 0;
            e->value_cap = 0;
            g_ht[found] = HT_DELETED; // evict from hash table
            insert_slot = found; // reuse this ht slot for the new entry
        } else {
            // genuine duplicate with a real existing value: append if distinct
            bool already = false;
            for (size_t j = 0; j < e->value_count; j++) {
                if (e->values[j] && strcmp(e->values[j], value) == 0) {
                    already = true;
                    break;
                }
            }
            if (already) {
                free(value);
            } else {
                if (e->value_count >= e->value_cap) {
                    size_t new_cap = e->value_cap * 2;
                    char **tmp = realloc(e->values, new_cap * sizeof(char *));
                    if (!tmp) {
                        fprintf(stderr,
                            "entries_add: out of memory, cannot grow value "
                            "array for key '%s'\n",
                            e->key);
                        free(value);
                        free(key);
                        return;
                    }
                    e->values = tmp;
                    e->value_cap = new_cap;
                }
                e->values[e->value_count++] = value;
            }
            free(key); // duplicate key string is never stored
            return;
        }
    }

    // new key: grow the flat array if needed
    if (g_count >= g_capacity) {
        g_capacity = g_capacity ? g_capacity * 2 : 256;
        KvEntry *tmp_entries = realloc(g_entries, g_capacity * sizeof(KvEntry));
        if (!tmp_entries) {
            g_capacity /= 2; // roll back the capacity bump
            fprintf(stderr,
                "entries_add: out of memory, cannot grow entry array to %zu "
                "entries\n",
                g_capacity * 2);
            return; // drop this entry rather than corrupt the store
        }
        g_entries = tmp_entries;
    }

    // store the new entry with an initial single-element values array
    char **vals = malloc(4 * sizeof(char *));
    if (!vals) {
        fprintf(stderr,
            "entries_add: out of memory, cannot allocate values array for key "
            "'%s'\n",
            key);
        free(value);
        free(key);
        return;
    }
    vals[0] = value;
    g_entries[g_count].key = key;
    g_entries[g_count].values = vals;
    g_entries[g_count].value_count = 1;
    g_entries[g_count].value_cap = 4;
    g_ht[insert_slot] = g_count; // point the slot at the new array index
    g_ht_used++;
    g_count++;
}

/*
 * Free all entries and reset the store to empty.
 */
void entries_clear(void)
{
    for (size_t i = 0; i < g_count; i++) {
        free(g_entries[i].key);
        for (size_t j = 0; j < g_entries[i].value_count; j++) {
            free(g_entries[i].values[j]);
        }
        free(g_entries[i].values);
    }
    free(g_entries);
    g_entries = NULL;
    g_count = 0;
    g_capacity = 0;

    free(g_ht);
    g_ht = NULL;
    g_ht_cap = 0;
    g_ht_used = 0;
}

/*
 * Return a read-only pointer to the flat entry array.
 */
const KvEntry *entries_data(void)
{
    return g_entries;
}

/*
 * Return the number of entries currently in the store.
 */
size_t entries_count(void)
{
    return g_count;
}

/*
 * Validate a byte offset and return a pointer into the file buffer.
 */
static inline void *checked_ptr(
    const uint8_t *base, size_t file_size, uint32_t offset)
{
    if (offset >= (uint32_t)file_size) {
        return NULL;
    }
    return (void *)(base + offset);
}

/*
 * Decode one decompressed entry payload and push it into the store.
 */
static void process_payload(
    uint16_t key_type_and_cat, const uint8_t *data, size_t size)
{
    if (size < 2) {
        // too short to contain even the key-length field, skip
        fprintf(stderr,
            "process_payload: payload too short (%zu bytes), skipping\n",
            size);
        return;
    }

    uint16_t key_len;
    memcpy(&key_len, data, 2); // memcpy avoids potential unaligned access
    if ((size_t)key_len + 2u > size) {
        // key extends past the end of the payload, skip this entry
        fprintf(stderr,
            "process_payload: key length %u exceeds payload size %zu, "
            "skipping\n",
            key_len,
            size);
        return;
    }

    const uint8_t *key_data = data + 2;
    const uint8_t *value_data = key_data + key_len;
    size_t value_len = size - 2u - key_len;

    // extract the key type from the low 3 bits of the packed field
    char *key = key_to_string(key_type_and_cat & 0x7u, key_data, key_len);
    if (!key) {
        fprintf(stderr,
            "process_payload: out of memory decoding key, skipping entry\n");
        return;
    }

    // values are always stored as UTF-16-LE strings
    char *value = utf16le_to_utf8(value_data, (uint16_t)value_len);
    if (!value) {
        fprintf(stderr,
            "process_payload: out of memory decoding value for key '%s', "
            "skipping entry\n",
            key);
        free(key);
        return;
    }

    entries_add(key, value); // ownership of both strings transfers here
}

/*
 * Walk a singly-linked entry chain iteratively.
 */
static void walk_chain(
    const uint8_t *base, size_t file_size, KvromAddress address)
{
    while (address.offset != 0u) {
        // validate the offset before dereferencing
        const KvromEntryHeader *hdr = (const KvromEntryHeader *)checked_ptr(
            base, file_size, address.offset);
        if (!hdr) {
            fprintf(stderr,
                "walk_chain: entry offset 0x%08X is out of bounds (file size "
                "%zu), stopping chain traversal\n",
                address.offset,
                file_size);
            break; // corrupt chain offset, stop walking
        }

        // the payload immediately follows the fixed-size entry header
        const uint8_t *payload = (const uint8_t *)(hdr + 1);

        switch (hdr->compressionType) {
        case 0: // uncompressed: pass the payload directly to the decoder
            process_payload(
                hdr->keyTypeAndDataCategory, payload, (size_t)hdr->dataSize);
            break;

        case 1: // raw Deflate
            if (hdr->dataSize > 4) {
                size_t out_size;
                // read the 4-byte little-endian decompressed-size hint
                uint32_t hint;
                memcpy(&hint, payload, 4);
                uint8_t *dec = decompress_raw_deflate(
                    payload + 4, // skip the 4-byte size hint
                    (size_t)hdr->dataSize - 4u,
                    &out_size,
                    (size_t)hint); // pass hint to avoid realloc churn
                if (dec) {
                    process_payload(hdr->keyTypeAndDataCategory, dec, out_size);
                    free(dec);
                } else {
                    fprintf(stderr,
                        "walk_chain: decompression failed for entry at offset "
                        "0x%08X, skipping\n",
                        address.offset);
                }
            } else {
                fprintf(stderr,
                    "walk_chain: compressed entry at offset 0x%08X is too "
                    "short to contain a size hint (%u bytes), skipping\n",
                    address.offset,
                    hdr->dataSize);
            }
            break;

        default:
            // unknown compression type: skip this entry with a warning
            fprintf(stderr,
                "walk_chain: unknown compression type %u at offset 0x%08X, "
                "skipping entry\n",
                hdr->compressionType,
                address.offset);
            break;
        }

        // advance to the next entry in the chain
        address = hdr->nextAddress;
    }
}

/*
 * Parse all hash-table chains and populate the global entry store.
 */
bool kvrom_parse(const uint8_t *data, size_t size)
{
    if (size < sizeof(KvromFileHeader)) {
        fprintf(stderr,
            "kvrom_parse: file too small to contain a header (%zu bytes, need "
            "%zu)\n",
            size,
            sizeof(KvromFileHeader));
        return false;
    }

    const KvromFileHeader *hdr = (const KvromFileHeader *)data;
    if (hdr->magicNumber != KVROM_MAGIC) {
        return false;
    }

    // locate the hash-table array
    const KvromAddress *table = (const KvromAddress *)checked_ptr(
        data, size, hdr->hashTableAddress.offset);
    if (!table) {
        fprintf(stderr,
            "kvrom_parse: hash table offset 0x%08X is out of bounds (file size "
            "%zu)\n",
            hdr->hashTableAddress.offset,
            size);
        return false;
    }

    // iterate over every bucket and walk its chain
    for (int32_t i = 0; i < hdr->hashTableSize; i++) {
        walk_chain(data, size, table[i]);
    }

    return true;
}
