/*
 *  filename: ini.c
 *  description: A simple INI configuration parser.
 *  author: Rainy101112(https://github.com/Rainy101112)
 *  date: Nov 15th, 2026
 *  license: MIT license
 */

#include <ctype.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "ini.h"

/* Starting size of the per-line buffer; it grows as needed, so a line may be
 * arbitrarily long. */
#define INI_LINE_INITIAL_CAPACITY 128

/* Hash index tuning: both tables start at INI_BUCKETS_INITIAL buckets (a power
 * of two, so it is always a multiple of INI_LOAD_FACTOR_DEN) and double once
 * the load factor would exceed NUM/DEN = 3/4. */
#define INI_BUCKETS_INITIAL 8
#define INI_LOAD_FACTOR_NUM 3
#define INI_LOAD_FACTOR_DEN 4

static char *ini_strdup(const char *src)
{
    size_t len;
    char *copy;

    if (src == NULL)
    {
        return NULL;
    }

    len = strlen(src);
    copy = (char *)malloc(len + 1);
    if (copy == NULL)
    {
        return NULL;
    }

    memcpy(copy, src, len + 1);
    return copy;
}

static char *trim_left(char *text)
{
    while (*text != '\0' && isspace((unsigned char)*text))
    {
        text++;
    }

    return text;
}

static void trim_right(char *text)
{
    size_t len = strlen(text);

    while (len > 0 && isspace((unsigned char)text[len - 1]))
    {
        text[len - 1] = '\0';
        len--;
    }
}

static char *trim(char *text)
{
    char *trimmed = trim_left(text);

    trim_right(trimmed);
    return trimmed;
}

/*
 * Remove an inline comment. ';' and '#' start a comment only when they are
 * preceded by whitespace (or appear at the start of the line), and only when
 * they are not inside a quoted value:
 *
 *   a = value ; dropped        -> "value"
 *   b = a#b                    -> "a#b"
 *   c = "a # b"                -> "\"a # b\""
 *
 * A quote opens a string only at the start of a token (after whitespace, after
 * '=' or at the beginning of the line), so an apostrophe in `it's` is literal.
 * There is no escape mechanism and quotes do not nest.
 */
static void strip_inline_comment(char *text)
{
    int in_whitespace = 1;
    int in_token_start = 1;
    char quote = '\0';

    while (*text != '\0')
    {
        if (quote != '\0')
        {
            if (*text == quote)
            {
                /* After a closing quote the next character is part of a value
                 * token, so a following '#' or ';' is not a comment marker. */
                quote = '\0';
                in_whitespace = 0;
                in_token_start = 0;
            }
            text++;
            continue;
        }

        if ((*text == '"' || *text == '\'') && in_token_start)
        {
            quote = *text;
            in_whitespace = 0;
            in_token_start = 0;
            text++;
            continue;
        }

        if ((*text == ';' || *text == '#') && in_whitespace)
        {
            *text = '\0';
            return;
        }

        in_whitespace = isspace((unsigned char)*text);
        in_token_start = (in_whitespace || *text == '=');
        text++;
    }
}

/*
 * Drop the surrounding quotes of a value so that `"  x  "` becomes `  x  `.
 * Values that are not completely quoted are returned unchanged.
 */
static char *unquote(char *value)
{
    size_t len = strlen(value);
    char quote;

    if (len < 2)
    {
        return value;
    }

    quote = value[0];
    if ((quote != '"' && quote != '\'') || value[len - 1] != quote)
    {
        return value;
    }

    value[len - 1] = '\0';
    return value + 1;
}

/*
 * Keyed hash for the lookup indexes: SipHash-2-4, the PRF designed for exactly
 * this job (Aumasson & Bernstein). Each parser draws its own 128 bit key, so an
 * attacker who controls the configuration file cannot precompute names that
 * all land in one bucket.
 */

static uint64_t rotl64(uint64_t value, int bits)
{
    return (value << bits) | (value >> (64 - bits));
}

static uint64_t read_le64(const unsigned char *bytes)
{
    return (uint64_t)bytes[0] |
           ((uint64_t)bytes[1] << 8) |
           ((uint64_t)bytes[2] << 16) |
           ((uint64_t)bytes[3] << 24) |
           ((uint64_t)bytes[4] << 32) |
           ((uint64_t)bytes[5] << 40) |
           ((uint64_t)bytes[6] << 48) |
           ((uint64_t)bytes[7] << 56);
}

#define INI_SIPROUND(v0, v1, v2, v3)                                           \
    do                                                                        \
    {                                                                         \
        v0 += v1;                                                             \
        v1 = rotl64(v1, 13);                                                  \
        v1 ^= v0;                                                             \
        v0 = rotl64(v0, 32);                                                  \
        v2 += v3;                                                             \
        v3 = rotl64(v3, 16);                                                  \
        v3 ^= v2;                                                             \
        v0 += v3;                                                             \
        v3 = rotl64(v3, 21);                                                  \
        v3 ^= v0;                                                             \
        v2 += v1;                                                             \
        v1 = rotl64(v1, 17);                                                  \
        v1 ^= v2;                                                             \
        v2 = rotl64(v2, 32);                                                  \
    } while (0)

static uint64_t siphash24(uint64_t key0, uint64_t key1,
                          const unsigned char *data, size_t length)
{
    uint64_t v0 = 0x736f6d6570736575ULL ^ key0;
    uint64_t v1 = 0x646f72616e646f6dULL ^ key1;
    uint64_t v2 = 0x6c7967656e657261ULL ^ key0;
    uint64_t v3 = 0x7465646279746573ULL ^ key1;
    size_t words = length - (length % 8);
    size_t index;
    uint64_t last = (uint64_t)length << 56;

    for (index = 0; index < words; index += 8)
    {
        uint64_t block = read_le64(data + index);

        v3 ^= block;
        INI_SIPROUND(v0, v1, v2, v3);
        INI_SIPROUND(v0, v1, v2, v3);
        v0 ^= block;
    }

    for (index = words; index < length; index++)
    {
        last |= (uint64_t)data[index] << (8 * (index - words));
    }

    v3 ^= last;
    INI_SIPROUND(v0, v1, v2, v3);
    INI_SIPROUND(v0, v1, v2, v3);
    v0 ^= last;

    v2 ^= 0xff;
    INI_SIPROUND(v0, v1, v2, v3);
    INI_SIPROUND(v0, v1, v2, v3);
    INI_SIPROUND(v0, v1, v2, v3);
    INI_SIPROUND(v0, v1, v2, v3);

    return v0 ^ v1 ^ v2 ^ v3;
}

static uint64_t hash_string(const ini_parser_t *parser, const char *text)
{
    return siphash24(parser->hash_key[0], parser->hash_key[1],
                     (const unsigned char *)text, strlen(text));
}

/*
 * Best effort OS entropy for the default hash key. Reading /dev/urandom with
 * plain stdio keeps the library free of feature-test macros and platform
 * headers; failing to open it is not fatal, seed_hash_key() falls back to a
 * portable mix.
 */
static int read_os_entropy(unsigned char *buffer, size_t size)
{
#if defined(_WIN32)
    (void)buffer;
    (void)size;
    return -1;
#else
    FILE *file = fopen("/dev/urandom", "rb");
    size_t got;

    if (file == NULL)
    {
        return -1;
    }

    got = fread(buffer, 1, size, file);
    fclose(file);
    return (got == size) ? 0 : -1;
#endif
}

/*
 * Give the parser a fresh 128 bit hash key. The OS entropy source is preferred;
 * the portable mix keeps the key unpredictable across runs even when it is not
 * available, because ASLR addresses and the clock differ per run. Neither path
 * is a substitute for calling ini_parser_set_hash_seed() with a CSPRNG value
 * when parsing untrusted input on a platform without /dev/urandom.
 */
static void seed_hash_key(ini_parser_t *parser)
{
    unsigned char raw[16];
    unsigned char *heap;
    uint64_t mix;

    if (read_os_entropy(raw, sizeof(raw)) == 0)
    {
        parser->hash_key[0] = read_le64(raw);
        parser->hash_key[1] = read_le64(raw + 8);
    }
    else
    {
        parser->hash_key[0] = 0;
        parser->hash_key[1] = 0;
    }

    heap = (unsigned char *)malloc(1);
    mix = (uint64_t)(uintptr_t)parser;
    mix ^= rotl64((uint64_t)(uintptr_t)heap, 17);
    mix *= 0x9e3779b97f4a7c15ULL;
    parser->hash_key[0] ^= mix;

    mix = ((uint64_t)time(NULL) * 0x2545f4914f6cdd1dULL) ^ (uint64_t)clock();
    mix ^= mix >> 29;
    parser->hash_key[1] ^= mix;

    free(heap);

    /* An all-zero key would be fully predictable, so never allow it. */
    if (parser->hash_key[0] == 0 && parser->hash_key[1] == 0)
    {
        parser->hash_key[0] = 0x0706050403020100ULL;
        parser->hash_key[1] = 0x0f0e0d0c0b0a0908ULL;
    }

    parser->hash_seeded = 1;
}

/*
 * Relink every node into the existing bucket arrays after the hash key changed.
 * The bucket counts were already sized for the current number of entries, so
 * this needs no allocation and cannot fail.
 */
static void reindex_all(ini_parser_t *parser)
{
    ini_section_t *section;
    size_t index;

    if (parser->section_buckets != NULL)
    {
        for (index = 0; index < parser->section_bucket_count; index++)
        {
            parser->section_buckets[index] = NULL;
        }

        for (section = parser->sections; section != NULL; section = section->next)
        {
            index = (size_t)(hash_string(parser, section->name) %
                             parser->section_bucket_count);
            section->hash_next = parser->section_buckets[index];
            parser->section_buckets[index] = section;
        }
    }

    for (section = parser->sections; section != NULL; section = section->next)
    {
        ini_key_t *key;

        if (section->key_buckets == NULL)
        {
            continue;
        }

        for (index = 0; index < section->key_bucket_count; index++)
        {
            section->key_buckets[index] = NULL;
        }

        for (key = section->head; key != NULL; key = key->next)
        {
            index = (size_t)(hash_string(parser, key->name) % section->key_bucket_count);
            key->hash_next = section->key_buckets[index];
            section->key_buckets[index] = key;
        }
    }
}

/* Grow to twice the number of buckets and relink every section. The sections
 * are rehashed by walking the file-order list, so the index never has to know
 * about bucket chains that were already built. */
static int section_table_grow(ini_parser_t *parser)
{
    size_t wanted = parser->section_bucket_count * 2;
    ini_section_t **buckets;
    ini_section_t *section;

    if (wanted < parser->section_bucket_count)
    {
        return -1; /* size_t overflow */
    }

    buckets = (ini_section_t **)calloc(wanted, sizeof(*buckets));
    if (buckets == NULL)
    {
        return -1;
    }

    for (section = parser->sections; section != NULL; section = section->next)
    {
        size_t index = (size_t)(hash_string(parser, section->name) % wanted);

        section->hash_next = buckets[index];
        buckets[index] = section;
    }

    free(parser->section_buckets);
    parser->section_buckets = buckets;
    parser->section_bucket_count = wanted;
    return 0;
}

/* Add a section to the index. The caller links it into the file-order list
 * afterwards, so this must not fail once it returns 0. */
static int section_table_insert(ini_parser_t *parser, ini_section_t *section)
{
    size_t threshold;
    size_t index;

    if (parser->section_buckets == NULL)
    {
        parser->section_buckets = (ini_section_t **)calloc(INI_BUCKETS_INITIAL,
                                                          sizeof(*parser->section_buckets));
        if (parser->section_buckets == NULL)
        {
            return -1;
        }
        parser->section_bucket_count = INI_BUCKETS_INITIAL;
    }
    else
    {
        threshold = (parser->section_bucket_count / INI_LOAD_FACTOR_DEN) *
                    INI_LOAD_FACTOR_NUM;
        if (parser->section_count + 1 > threshold && section_table_grow(parser) != 0)
        {
            return -1;
        }
    }

    index = (size_t)(hash_string(parser, section->name) % parser->section_bucket_count);
    section->hash_next = parser->section_buckets[index];
    parser->section_buckets[index] = section;
    parser->section_count++;

    return 0;
}

static int key_table_grow(const ini_parser_t *parser, ini_section_t *section)
{
    size_t wanted = section->key_bucket_count * 2;
    ini_key_t **buckets;
    ini_key_t *key;

    if (wanted < section->key_bucket_count)
    {
        return -1;
    }

    buckets = (ini_key_t **)calloc(wanted, sizeof(*buckets));
    if (buckets == NULL)
    {
        return -1;
    }

    for (key = section->head; key != NULL; key = key->next)
    {
        size_t index = (size_t)(hash_string(parser, key->name) % wanted);

        key->hash_next = buckets[index];
        buckets[index] = key;
    }

    free(section->key_buckets);
    section->key_buckets = buckets;
    section->key_bucket_count = wanted;
    return 0;
}

static int key_table_insert(const ini_parser_t *parser, ini_section_t *section,
                            ini_key_t *key)
{
    size_t threshold;
    size_t index;

    if (section->key_buckets == NULL)
    {
        section->key_buckets = (ini_key_t **)calloc(INI_BUCKETS_INITIAL,
                                                    sizeof(*section->key_buckets));
        if (section->key_buckets == NULL)
        {
            return -1;
        }
        section->key_bucket_count = INI_BUCKETS_INITIAL;
    }
    else
    {
        threshold = (section->key_bucket_count / INI_LOAD_FACTOR_DEN) *
                    INI_LOAD_FACTOR_NUM;
        if (section->key_count + 1 > threshold && key_table_grow(parser, section) != 0)
        {
            return -1;
        }
    }

    index = (size_t)(hash_string(parser, key->name) % section->key_bucket_count);
    key->hash_next = section->key_buckets[index];
    section->key_buckets[index] = key;
    section->key_count++;

    return 0;
}

static ini_section_t *find_section(const ini_parser_t *parser, const char *name)
{
    size_t index;
    ini_section_t *section;

    if (parser->section_bucket_count == 0)
    {
        return NULL;
    }

    index = (size_t)(hash_string(parser, name) % parser->section_bucket_count);
    for (section = parser->section_buckets[index]; section != NULL;
         section = section->hash_next)
    {
        if (strcmp(section->name, name) == 0)
        {
            return section;
        }
    }

    return NULL;
}

static ini_key_t *find_key(const ini_parser_t *parser, const ini_section_t *section,
                           const char *name)
{
    size_t index;
    ini_key_t *key;

    if (section->key_bucket_count == 0)
    {
        return NULL;
    }

    index = (size_t)(hash_string(parser, name) % section->key_bucket_count);
    for (key = section->key_buckets[index]; key != NULL; key = key->hash_next)
    {
        if (strcmp(key->name, name) == 0)
        {
            return key;
        }
    }

    return NULL;
}

/* Create a section, index it and append it to the file-order list. */
static int create_section(ini_parser_t *parser, const char *name, ini_section_t **section_out)
{
    ini_section_t *section = (ini_section_t *)calloc(1, sizeof(ini_section_t));

    if (section == NULL)
    {
        return -1;
    }

    section->name = ini_strdup(name);
    if (section->name == NULL)
    {
        free(section);
        return -1;
    }

    if (section_table_insert(parser, section) != 0)
    {
        free(section->name);
        free(section);
        return -1;
    }

    if (parser->tail == NULL)
    {
        parser->sections = section;
    }
    else
    {
        parser->tail->next = section;
    }
    parser->tail = section;

    *section_out = section;
    return 0;
}

static int set_key_value(ini_parser_t *parser, ini_section_t *section,
                         const char *name, const char *value)
{
    ini_key_t *key = find_key(parser, section, name);
    char *value_copy;

    if (key != NULL)
    {
        value_copy = ini_strdup(value);
        if (value_copy == NULL)
        {
            return -1;
        }
        free(key->value);
        key->value = value_copy;
        return 0;
    }

    key = (ini_key_t *)calloc(1, sizeof(ini_key_t));
    if (key == NULL)
    {
        return -1;
    }

    key->name = ini_strdup(name);
    key->value = ini_strdup(value);
    if (key->name == NULL || key->value == NULL)
    {
        free(key->name);
        free(key->value);
        free(key);
        return -1;
    }

    if (key_table_insert(parser, section, key) != 0)
    {
        free(key->name);
        free(key->value);
        free(key);
        return -1;
    }

    if (section->tail == NULL)
    {
        section->head = key;
    }
    else
    {
        section->tail->next = key;
    }
    section->tail = key;

    return 0;
}

/*
 * Read one line of arbitrary length into *buffer, growing it when needed.
 *
 * Returns 1 when a line was read, 0 at end of file and a negative value on
 * failure: -1 for an allocation error, -2 for a read error.
 */
static int read_line(FILE *file, char **buffer, size_t *capacity)
{
    size_t length = 0;

    if (*buffer == NULL || *capacity == 0)
    {
        char *fresh = (char *)malloc(INI_LINE_INITIAL_CAPACITY);

        if (fresh == NULL)
        {
            return -1;
        }

        *buffer = fresh;
        *capacity = INI_LINE_INITIAL_CAPACITY;
    }

    (*buffer)[0] = '\0';

    for (;;)
    {
        size_t free_space;
        size_t added;

        if (length + 1 >= *capacity)
        {
            size_t wanted = *capacity * 2;
            char *grown;

            if (wanted <= *capacity)
            {
                return -1; /* overflow */
            }

            grown = (char *)realloc(*buffer, wanted);
            if (grown == NULL)
            {
                return -1;
            }

            *buffer = grown;
            *capacity = wanted;
        }

        /* Capacity includes the terminating NUL, so this is the largest chunk
         * fgets() can fill without overflowing. */
        free_space = *capacity - length;
        if (free_space > (size_t)INT_MAX)
        {
            free_space = (size_t)INT_MAX;
        }

        if (fgets(*buffer + length, (int)free_space, file) == NULL)
        {
            if (length == 0)
            {
                return 0; /* clean end of file */
            }

            /* Last line without a trailing newline. */
            return ferror(file) ? -2 : 1;
        }

        added = strlen(*buffer + length);
        length += added;

        if (length > 0 && (*buffer)[length - 1] == '\n')
        {
            return 1;
        }

        /* fgets() stopped before filling the buffer without finding a newline,
         * so this is a complete last line. */
        if (added + 1 < free_space)
        {
            return 1;
        }
    }
}

void ini_parser_init(ini_parser_t *parser)
{
    if (parser == NULL)
    {
        return;
    }

    parser->sections = NULL;
    parser->last_error = INI_OK;
    parser->last_error_line = 0;
    parser->tail = NULL;
    parser->section_buckets = NULL;
    parser->section_bucket_count = 0;
    parser->section_count = 0;
    /* The hash key is drawn lazily on the first load, so that creating a parser
     * never touches the OS entropy source. */
    parser->hash_key[0] = 0;
    parser->hash_key[1] = 0;
    parser->hash_seeded = 0;
}

void ini_parser_set_hash_seed(ini_parser_t *parser, uint64_t seed0, uint64_t seed1)
{
    if (parser == NULL)
    {
        return;
    }

    parser->hash_key[0] = seed0;
    parser->hash_key[1] = seed1;
    parser->hash_seeded = 1;

    reindex_all(parser);
}

int ini_parser_load(ini_parser_t *parser, const char *path)
{
    FILE *file;
    char *line = NULL;
    size_t line_capacity = 0;
    ini_section_t *current_section = NULL;
    int line_number = 0;
    int status = INI_OK;

    if (parser == NULL || path == NULL)
    {
        return INI_ERROR_ARGUMENT;
    }

    ini_parser_free(parser);
    parser->last_error = INI_OK;
    parser->last_error_line = 0;

    if (!parser->hash_seeded)
    {
        seed_hash_key(parser);
    }

    file = fopen(path, "r");
    if (file == NULL)
    {
        parser->last_error = INI_ERROR_OPEN;
        return INI_ERROR_OPEN;
    }

    for (;;)
    {
        int read_status = read_line(file, &line, &line_capacity);
        char *content;
        char *equals;

        if (read_status <= 0)
        {
            line_number++; /* the line we tried to read */
            if (read_status == -1)
            {
                status = INI_ERROR_MEMORY;
            }
            else if (read_status == -2)
            {
                status = INI_ERROR_READ;
            }
            break;
        }
        line_number++;

        content = trim(line);
        strip_inline_comment(content);
        content = trim(content);
        if (*content == '\0')
        {
            continue;
        }

        if (*content == '[')
        {
            char *end = strchr(content, ']');

            if (end == NULL)
            {
                status = INI_ERROR_SYNTAX;
                break;
            }

            *end = '\0';
            content = trim(content + 1);
            if (*content == '\0')
            {
                status = INI_ERROR_SYNTAX;
                break;
            }

            current_section = find_section(parser, content);
            if (current_section == NULL &&
                create_section(parser, content, &current_section) != 0)
            {
                status = INI_ERROR_MEMORY;
                break;
            }
            continue;
        }

        equals = strchr(content, '=');
        if (equals == NULL)
        {
            status = INI_ERROR_SYNTAX;
            break;
        }

        *equals = '\0';
        {
            char *key = trim(content);
            char *value = unquote(trim(equals + 1));

            if (*key == '\0')
            {
                status = INI_ERROR_SYNTAX;
                break;
            }

            /* Keys before the first header go into the default section. */
            if (current_section == NULL)
            {
                current_section = find_section(parser, INI_DEFAULT_SECTION);
                if (current_section == NULL &&
                    create_section(parser, INI_DEFAULT_SECTION, &current_section) != 0)
                {
                    status = INI_ERROR_MEMORY;
                    break;
                }
            }

            if (set_key_value(parser, current_section, key, value) != 0)
            {
                status = INI_ERROR_MEMORY;
                break;
            }
        }
    }

    free(line);
    fclose(file);

    if (status != INI_OK)
    {
        ini_parser_free(parser);
        parser->last_error = status;
        parser->last_error_line = line_number;
        return status;
    }

    return INI_OK;
}

const char *ini_parser_get(const ini_parser_t *parser, const char *section, const char *key)
{
    ini_section_t *target_section;
    ini_key_t *target_key;

    if (parser == NULL || section == NULL || key == NULL)
    {
        return NULL;
    }

    target_section = find_section(parser, section);
    if (target_section == NULL)
    {
        return NULL;
    }

    target_key = find_key(parser, target_section, key);
    if (target_key == NULL)
    {
        return NULL;
    }

    return target_key->value;
}

int ini_parser_get_copy(const ini_parser_t *parser, const char *section,
                        const char *key, char *out, size_t out_size)
{
    const char *value;
    size_t len;

    if (out == NULL || out_size == 0)
    {
        return INI_ERROR_ARGUMENT;
    }

    out[0] = '\0';

    value = ini_parser_get(parser, section, key);
    if (value == NULL)
    {
        return INI_ERROR_NOT_FOUND;
    }

    len = strlen(value);
    if (len + 1 > out_size)
    {
        memcpy(out, value, out_size - 1);
        out[out_size - 1] = '\0';
        return INI_ERROR_TOO_SMALL;
    }

    memcpy(out, value, len + 1);
    return INI_OK;
}

void ini_parser_free(ini_parser_t *parser)
{
    ini_section_t *section;

    if (parser == NULL)
    {
        return;
    }

    section = parser->sections;
    while (section != NULL)
    {
        ini_section_t *next_section = section->next;
        ini_key_t *key = section->head;

        while (key != NULL)
        {
            ini_key_t *next_key = key->next;
            free(key->name);
            free(key->value);
            free(key);
            key = next_key;
        }

        free(section->key_buckets);
        free(section->name);
        free(section);
        section = next_section;
    }

    free(parser->section_buckets);

    parser->sections = NULL;
    parser->tail = NULL;
    parser->section_buckets = NULL;
    parser->section_bucket_count = 0;
    parser->section_count = 0;
}

const char *ini_strerror(int status)
{
    switch (status)
    {
    case INI_OK:
        return "success";
    case INI_ERROR:
        return "unspecified error";
    case INI_ERROR_ARGUMENT:
        return "invalid argument";
    case INI_ERROR_OPEN:
        return "cannot open file";
    case INI_ERROR_READ:
        return "read error";
    case INI_ERROR_SYNTAX:
        return "syntax error";
    case INI_ERROR_MEMORY:
        return "out of memory";
    case INI_ERROR_NOT_FOUND:
        return "section or key not found";
    case INI_ERROR_TOO_SMALL:
        return "destination buffer too small";
    default:
        return "unknown error";
    }
}
