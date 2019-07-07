/*
 * Lux ACPI Implementation
 * Copyright (C) 2018-2019 the lai authors
 */

#pragma once

struct lai_list_item {
    struct lai_list_item *next;
    struct lai_list_item *prev;
};

struct lai_list {
    struct lai_list_item hook;
};
