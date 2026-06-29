/*
 * Parse KVROM data.
 *
 * SPDX-FileCopyrightText: 2026 SombrAbsol
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef PARSE_H
#define PARSE_H

#include "kvrom.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Add a key/value pair to the global store.
 */
void entries_add(char *key, char *value);

/*
 * Free all entries and reset the store to empty.
 */
void entries_clear(void);

/*
 * Return a read-only pointer to the flat entry array.
 */
const KvEntry *entries_data(void);

/*
 * Return the number of entries currently in the store.
 */
size_t entries_count(void);

/*
 * Parse all hash-table chains and populate the global entry store.
 */
bool kvrom_parse(const uint8_t *data, size_t size);

#endif /* PARSE_H */
