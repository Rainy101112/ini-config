/*
 *  filename: ini.h
 *  description: A simple INI configuration parser.
 *  author: Rainy101112(https://github.com/Rainy101112)
 *  date: Nov 15th, 2026
 *  license: MIT license
 */

#ifndef INCLUDE_INI_H_
#define INCLUDE_INI_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Section used for keys that appear before the first "[section]" header.
 * It is the empty string, so ini_parser_get(&parser, "", "key") reads them.
 */
#define INI_DEFAULT_SECTION ""

typedef enum ini_status
{
    INI_OK = 0,
    INI_ERROR = -1,               /* unspecified failure (legacy catch-all) */
    INI_ERROR_ARGUMENT = -2,      /* NULL pointer or zero buffer size        */
    INI_ERROR_OPEN = -3,          /* cannot open the configuration file       */
    INI_ERROR_READ = -4,          /* read error while parsing                 */
    INI_ERROR_SYNTAX = -5,        /* malformed line; see last_error_line      */
    INI_ERROR_MEMORY = -6,        /* allocation failure                       */
    INI_ERROR_NOT_FOUND = -7,     /* section or key does not exist            */
    INI_ERROR_TOO_SMALL = -8      /* destination buffer too small             */
} ini_status_t;

typedef struct ini_key ini_key_t;
typedef struct ini_section ini_section_t;
typedef struct ini_parser ini_parser_t;

/*
 * The structs below are public so that a parsed file can be iterated without
 * extra API. Fields marked "internal" are owned by the parser: read them if
 * you like, but do not modify them or rely on their layout.
 *
 * Iteration order is the order of appearance in the file, and is independent
 * of the hash index used for lookups:
 *
 *   for (s = parser->sections; s; s = s->next)
 *       for (k = s->head; k; k = k->next)
 */
struct ini_key
{
    char *name;
    char *value;
    ini_key_t *next;      /* next key in file order            */
    ini_key_t *hash_next; /* internal: next key in the same bucket */
};

struct ini_section
{
    char *name;
    ini_key_t *head;
    ini_section_t *next;      /* next section in file order           */
    ini_key_t *tail;          /* internal: last key, for O(1) append  */
    ini_section_t *hash_next; /* internal: next section in the bucket */
    ini_key_t **key_buckets;  /* internal: hash index over keys       */
    size_t key_bucket_count;  /* internal: number of buckets, 0 if none */
    size_t key_count;         /* internal: number of keys             */
};

struct ini_parser
{
    ini_section_t *sections;
    int last_error;      /* ini_status_t of the most recent failure, INI_OK if none */
    int last_error_line; /* 1-based line of the most recent syntax/read failure     */

    /* internal: hash index over sections, kept sorted-independent of order */
    ini_section_t *tail;              /* last section, for O(1) append */
    ini_section_t **section_buckets;  /* hash index over sections      */
    size_t section_bucket_count;      /* number of buckets, 0 if none  */
    size_t section_count;             /* number of sections            */

    /* internal: keyed hash secret (SipHash-2-4), see ini_parser_set_hash_seed */
    uint64_t hash_key[2];
    int hash_seeded;
};

/* Zero a parser before first use. ini_parser_load() also works on a zero
 * initialised struct, but explicit initialisation is clearer and is required
 * for a struct that was used before. */
void ini_parser_init(ini_parser_t *parser);

/* Parse `path` into `parser`, discarding any previous contents.
 * Returns INI_OK or a negative ini_status_t; on failure `parser->sections`
 * is NULL and parser->last_error / last_error_line describe the problem. */
int ini_parser_load(ini_parser_t *parser, const char *path);

/* Return the value, or NULL when the section or key does not exist.
 * The pointer belongs to the parser: it is invalid after ini_parser_free()
 * or after the next ini_parser_load(). Use ini_parser_get_copy() to keep it. */
const char *ini_parser_get(const ini_parser_t *parser, const char *section,
                           const char *key);

/* Copy the value into `out` (always NUL terminated when out_size > 0).
 * Returns INI_OK, INI_ERROR_NOT_FOUND (out is set to ""),
 * INI_ERROR_TOO_SMALL (truncated copy) or INI_ERROR_ARGUMENT. */
int ini_parser_get_copy(const ini_parser_t *parser, const char *section,
                        const char *key, char *out, size_t out_size);

/* Release every section and key and reset parser->sections to NULL.
 * Safe to call on a zeroed parser and safe to call twice. */
void ini_parser_free(ini_parser_t *parser);

/* Human readable description of an ini_status_t value; never NULL. */
const char *ini_strerror(int status);

/*
 * Override the per-parser hash key.
 *
 * By default every parser draws a fresh 128 bit key the first time a file is
 * loaded, so an attacker who controls the configuration file cannot precompute
 * key names that all collide. Pass a value from your own CSPRNG if you want to
 * be independent of the library's entropy source, or a fixed value to make
 * behaviour reproducible (tests, reproducible builds).
 *
 * Safe to call at any time: an already populated parser is re-indexed in place
 * without allocating, and the file order is not affected.
 */
void ini_parser_set_hash_seed(ini_parser_t *parser, uint64_t seed0, uint64_t seed1);

#ifdef __cplusplus
}
#endif

#endif /* INCLUDE_INI_H_ */
