
/*
 * Lightweight ACPI Implementation
 * Copyright (C) 2018-2019 the lai authors
 */

#pragma once

size_t lai_parse_integer(uint8_t *object, uint64_t *integer);
size_t lai_parse_pkgsize(uint8_t *, size_t *);
int lai_is_name(char);
