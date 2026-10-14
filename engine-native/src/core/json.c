#include "json.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#define JSON_DEPTH_MAX 128

struct json_cur {
    const char *s;
    size_t n;
    size_t i;
    int depth;
    int error;
};

static void value_clear(struct json_value *value)
{
    if (!value)
        return;
    memset(value, 0, sizeof *value);
}

static int fail(struct json_cur *cur)
{
    cur->error = 1;
    return -1;
}

static void skip(struct json_cur *cur)
{
    while (cur->i < cur->n) {
        char c = cur->s[cur->i];

        if (c != ' ' && c != '\t' && c != '\r' && c != '\n')
            break;
        cur->i++;
    }
}

static int grow(void **ptr, uint32_t *cap, uint32_t count, size_t elem)
{
    uint32_t next;
    void *block;

    if (count < *cap)
        return 0;
    if (*cap > UINT32_MAX / 2u)
        return -1;
    next = *cap ? *cap * 2u : 4u;
    if (next < count + 1u)
        next = count + 1u;
    if (next > SIZE_MAX / elem)
        return -1;
    block = realloc(*ptr, (size_t)next * elem);
    if (!block)
        return -1;
    *ptr = block;
    *cap = next;
    return 0;
}

static char *take_text(const char *src, size_t n)
{
    char *out = (char *)malloc(n + 1u);

    if (!out)
        return NULL;
    if (n)
        memcpy(out, src, n);
    out[n] = '\0';
    return out;
}

static int parse_value(struct json_cur *cur, struct json_value *out);

static int parse_string(struct json_cur *cur, char **out)
{
    char *buf = NULL;
    uint32_t n = 0;
    uint32_t cap = 0;

    *out = NULL;
    if (cur->i >= cur->n || cur->s[cur->i] != '"')
        return fail(cur);
    cur->i++;
    while (cur->i < cur->n) {
        char c = cur->s[cur->i++];
        char extra[4];
        size_t extra_n = 1;

        extra[0] = c;
        if (c == '"') {
            *out = take_text(buf ? buf : "", n);
            free(buf);
            if (!*out)
                return fail(cur);
            return 0;
        }
        if (c == '\\') {
            if (cur->i >= cur->n)
                break;
            c = cur->s[cur->i++];
            if (c == 'n')
                extra[0] = '\n';
            else if (c == 'r')
                extra[0] = '\r';
            else if (c == 't')
                extra[0] = '\t';
            else if (c == 'u') {
                unsigned code = 0;
                int k;

                if (cur->i + 4u > cur->n) {
                    free(buf);
                    return fail(cur);
                }
                for (k = 0; k < 4; k++) {
                    char h = cur->s[cur->i++];

                    code <<= 4;
                    if (h >= '0' && h <= '9')
                        code += (unsigned)(h - '0');
                    else if (h >= 'a' && h <= 'f')
                        code += (unsigned)(h - 'a' + 10);
                    else if (h >= 'A' && h <= 'F')
                        code += (unsigned)(h - 'A' + 10);
                    else {
                        free(buf);
                        return fail(cur);
                    }
                }
                /* One code unit. Surrogate pairs stay two values, as AHK does. */
                if (code < 0x80u) {
                    extra[0] = (char)code;
                } else if (code < 0x800u) {
                    extra[0] = (char)(0xC0u | (code >> 6));
                    extra[1] = (char)(0x80u | (code & 0x3Fu));
                    extra_n = 2;
                } else {
                    extra[0] = (char)(0xE0u | (code >> 12));
                    extra[1] = (char)(0x80u | ((code >> 6) & 0x3Fu));
                    extra[2] = (char)(0x80u | (code & 0x3Fu));
                    extra_n = 3;
                }
            } else {
                extra[0] = c;
            }
        }
        if (n > UINT32_MAX - (uint32_t)extra_n ||
            grow((void **)&buf, &cap, n + (uint32_t)extra_n - 1u, 1) != 0) {
            free(buf);
            return fail(cur);
        }
        memcpy(buf + n, extra, extra_n);
        n += (uint32_t)extra_n;
    }
    free(buf);
    return fail(cur);
}

static int parse_number(struct json_cur *cur, struct json_value *out)
{
    size_t start = cur->i;
    int dotted = 0;
    int exponent = 0;
    char *token;
    char *end = NULL;

    if (cur->i < cur->n && cur->s[cur->i] == '-')
        cur->i++;
    while (cur->i < cur->n) {
        char c = cur->s[cur->i];

        if ((c >= '0' && c <= '9') || c == '.' || c == 'e' || c == 'E' || c == '+' || c == '-') {
            if (c == '.')
                dotted = 1;
            if (c == 'e' || c == 'E')
                exponent = 1;
            cur->i++;
            continue;
        }
        break;
    }
    if (cur->i == start || (cur->i == start + 1u && cur->s[start] == '-'))
        return fail(cur);
    token = take_text(cur->s + start, cur->i - start);
    if (!token)
        return fail(cur);
    if (!dotted && !exponent) {
        long long integer;

        errno = 0;
        integer = strtoll(token, &end, 10);
        if (end != token && !*end && errno != ERANGE) {
            out->kind = JSON_NUMBER;
            out->number_is_int = 1;
            out->integer = integer;
            out->number = (double)integer;
            free(token);
            return 0;
        }
        if (end == token || *end) {
            free(token);
            return fail(cur);
        }
        end = NULL;
    }
    out->number = strtod(token, &end);
    if (end == token || *end) {
        free(token);
        return fail(cur);
    }
    out->kind = JSON_NUMBER;
    free(token);
    return 0;
}

static int parse_array(struct json_cur *cur, struct json_value *out)
{
    uint32_t cap = 0;

    cur->i++;
    out->kind = JSON_ARRAY;
    skip(cur);
    if (cur->i < cur->n && cur->s[cur->i] == ']') {
        cur->i++;
        return 0;
    }
    for (;;) {
        struct json_value item;

        value_clear(&item);
        if (parse_value(cur, &item) != 0) {
            json_free(out);
            value_clear(out);
            return -1;
        }
        if (grow((void **)&out->items, &cap, out->length, sizeof *out->items) != 0) {
            json_free(&item);
            json_free(out);
            value_clear(out);
            return fail(cur);
        }
        out->items[out->length++] = item;
        skip(cur);
        if (cur->i < cur->n && cur->s[cur->i] == ',') {
            cur->i++;
            continue;
        }
        if (cur->i < cur->n && cur->s[cur->i] == ']') {
            cur->i++;
            return 0;
        }
        json_free(out);
        value_clear(out);
        return fail(cur);
    }
}

static int find_key(const struct json_value *object, const char *key)
{
    uint32_t i;

    if (!object || !key)
        return -1;
    for (i = 0; i < object->length; i++) {
        if (object->keys[i] && strcmp(object->keys[i], key) == 0)
            return (int)i;
    }
    return -1;
}

static int parse_object(struct json_cur *cur, struct json_value *out)
{
    uint32_t key_cap = 0;
    uint32_t value_cap = 0;

    cur->i++;
    out->kind = JSON_OBJECT;
    skip(cur);
    if (cur->i < cur->n && cur->s[cur->i] == '}') {
        cur->i++;
        return 0;
    }
    for (;;) {
        char *key = NULL;
        struct json_value item;
        int existing;

        value_clear(&item);
        skip(cur);
        if (parse_string(cur, &key) != 0) {
            json_free(out);
            value_clear(out);
            return -1;
        }
        skip(cur);
        if (cur->i >= cur->n || cur->s[cur->i] != ':') {
            free(key);
            json_free(out);
            value_clear(out);
            return fail(cur);
        }
        cur->i++;
        if (parse_value(cur, &item) != 0) {
            free(key);
            json_free(out);
            value_clear(out);
            return -1;
        }
        existing = find_key(out, key);
        if (existing >= 0) {
            json_free(&out->values[existing]);
            out->values[existing] = item;
            free(key);
        } else {
            if (grow((void **)&out->keys, &key_cap, out->length, sizeof *out->keys) != 0 ||
                grow((void **)&out->values, &value_cap, out->length, sizeof *out->values) != 0) {
                free(key);
                json_free(&item);
                json_free(out);
                value_clear(out);
                return fail(cur);
            }
            out->keys[out->length] = key;
            out->values[out->length] = item;
            out->length++;
        }
        skip(cur);
        if (cur->i < cur->n && cur->s[cur->i] == ',') {
            cur->i++;
            continue;
        }
        if (cur->i < cur->n && cur->s[cur->i] == '}') {
            cur->i++;
            return 0;
        }
        json_free(out);
        value_clear(out);
        return fail(cur);
    }
}

static int parse_word(struct json_cur *cur, size_t skip_n, int kind, int boolean, struct json_value *out)
{
    size_t left = cur->n - cur->i;

    if (left < skip_n)
        return fail(cur);
    cur->i += skip_n;
    out->kind = kind;
    out->boolean = boolean;
    if (kind == JSON_NULL) {
        out->text = take_text("", 0);
        if (!out->text)
            return fail(cur);
    }
    return 0;
}

static int parse_value(struct json_cur *cur, struct json_value *out)
{
    char c;

    value_clear(out);
    skip(cur);
    if (cur->i >= cur->n)
        return fail(cur);
    if (cur->depth >= JSON_DEPTH_MAX)
        return fail(cur);
    c = cur->s[cur->i];
    if (c == '{') {
        cur->depth++;
        if (parse_object(cur, out) != 0)
            return -1;
        cur->depth--;
        return cur->error ? -1 : 0;
    }
    if (c == '[') {
        cur->depth++;
        if (parse_array(cur, out) != 0)
            return -1;
        cur->depth--;
        return cur->error ? -1 : 0;
    }
    if (c == '"') {
        if (parse_string(cur, &out->text) != 0)
            return -1;
        out->kind = JSON_STRING;
        return 0;
    }
    if (c == 't')
        return parse_word(cur, 4, JSON_BOOL, 1, out);
    if (c == 'f')
        return parse_word(cur, 5, JSON_BOOL, 0, out);
    if (c == 'n')
        return parse_word(cur, 4, JSON_NULL, 0, out);
    return parse_number(cur, out);
}

int json_parse(const char *text, size_t length, struct json_value *out)
{
    struct json_cur cur;

    if (!out)
        return -1;
    value_clear(out);
    if (!text)
        return -1;
    memset(&cur, 0, sizeof cur);
    cur.s = text;
    cur.n = length;
    if (length >= 3u && (unsigned char)text[0] == 0xEFu && (unsigned char)text[1] == 0xBBu &&
        (unsigned char)text[2] == 0xBFu)
        cur.i = 3;
    if (parse_value(&cur, out) != 0 || cur.error) {
        json_free(out);
        value_clear(out);
        return -1;
    }
    return 0;
}

void json_free(struct json_value *value)
{
    uint32_t i;

    if (!value)
        return;
    free(value->text);
    value->text = NULL;
    if (value->items) {
        for (i = 0; i < value->length; i++)
            json_free(&value->items[i]);
        free(value->items);
    }
    if (value->keys) {
        for (i = 0; i < value->length; i++)
            free(value->keys[i]);
        free(value->keys);
    }
    if (value->values) {
        for (i = 0; i < value->length; i++)
            json_free(&value->values[i]);
        free(value->values);
    }
    value_clear(value);
}

const struct json_value *json_object_get(const struct json_value *object, const char *key)
{
    int index;

    if (!object || object->kind != JSON_OBJECT)
        return NULL;
    index = find_key(object, key);
    if (index < 0)
        return NULL;
    return &object->values[index];
}
