
/*
 * Lux ACPI Implementation
 * Copyright (C) 2018-2019 the lai authors
 */

#pragma once

// LAI internal header

void *lai_calloc(size_t, size_t);
size_t lai_strlen(const char *);
char *lai_strcpy(char *, const char *);
int lai_strcmp(const char *, const char *);

void lai_debug(const char *, ...);
void lai_warn(const char *, ...);
__attribute__((noreturn)) void lai_panic(const char *, ...);
