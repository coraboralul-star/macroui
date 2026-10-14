/* Profile JSON. Objects, arrays, and strings match engine/JSON.ahk.
   null is an empty string. A leading UTF-8 BOM is skipped.
   Duplicate object keys keep the last value. Trailing text is ignored.
   This file does not know about macros. */
#ifndef ENGINE_JSON_H
#define ENGINE_JSON_H

#include <stddef.h>
#include <stdint.h>

#define JSON_NONE 0
#define JSON_NULL 1
#define JSON_BOOL 2
#define JSON_NUMBER 3
#define JSON_STRING 4
#define JSON_ARRAY 5
#define JSON_OBJECT 6

struct json_value {
    int kind;
    int boolean;
    int number_is_int;
    double number;
    long long integer;
    char *text;
    struct json_value *items;
    uint32_t length;
    char **keys;
    struct json_value *values;
};

/* 0 on a value. -1 leaves *out cleared. A null text pointer is an empty document. */
int json_parse(const char *text, size_t length, struct json_value *out);

void json_free(struct json_value *value);

/* Last matching key. NULL when the key is absent. */
const struct json_value *json_object_get(const struct json_value *object, const char *key);

#endif
