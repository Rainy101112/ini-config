/*
 *  filename: tests/flood_demo.c
 *  description: Demonstrates that keying the hash defeats a collision flood.
 *  author: Rainy101112(https://github.com/Rainy101112)
 *  license: MIT license
 *
 *  Run with `make flood-demo`. It is not part of `make test` because it is slow
 *  (it brute forces colliding key names) and it reports numbers rather than just
 *  pass/fail.
 *
 *  The scenario: an attacker knows the parser's hash key (worst case, e.g. the
 *  secret leaked) and crafts a configuration whose key names all collide. That
 *  really does collapse the table. The same file parsed by a parser with the
 *  default random key spreads out normally, which is the whole point of keying
 *  the hash.
 *
 *  White-box: includes the implementation to reach the internal hasher and the
 *  bucket arrays.
 */

#include "../ini.c"

#include <stdio.h>

#define KEY_COUNT 5000
#define NAME_BUFFER 16
/* Bucket count the key table reaches with KEY_COUNT keys; collisions are
 * crafted modulo this so that the chains survive the growth rehashes. */
#define TARGET_BUCKETS 1024

/* What the attacker knows in this simulation. */
static const uint64_t g_leaked_key0 = 0x0123456789abcdefULL;
static const uint64_t g_leaked_key1 = 0xfedcba9876543210ULL;

static volatile unsigned long g_sink;

static double now_ms(void)
{
    return (double)clock() * 1000.0 / (double)CLOCKS_PER_SEC;
}

static size_t longest_chain(const ini_section_t *section)
{
    size_t index;
    size_t best = 0;

    for (index = 0; index < section->key_bucket_count; index++)
    {
        size_t length = 0;
        const ini_key_t *key;

        for (key = section->key_buckets[index]; key != NULL; key = key->hash_next)
        {
            length++;
        }

        if (length > best)
        {
            best = length;
        }
    }

    return best;
}

static void measure(char (*names)[NAME_BUFFER], const char *path, const char *label,
                    int use_default_key, size_t *chain_out, double *ns_per_get)
{
    ini_parser_t parser;
    double started;
    double load_ms;
    double get_ms;
    size_t i;

    ini_parser_init(&parser);

    if (!use_default_key)
    {
        ini_parser_set_hash_seed(&parser, g_leaked_key0, g_leaked_key1);
    }

    started = now_ms();
    if (ini_parser_load(&parser, path) != INI_OK)
    {
        fprintf(stderr, "%s: load failed\n", label);
        *chain_out = 0;
        *ns_per_get = 0;
        ini_parser_free(&parser);
        return;
    }
    load_ms = now_ms() - started;

    *chain_out = (parser.sections != NULL) ? longest_chain(parser.sections) : 0;

    started = now_ms();
    for (i = 0; i < KEY_COUNT; i++)
    {
        const char *value = ini_parser_get(&parser, "s", names[i]);

        if (value == NULL)
        {
            fprintf(stderr, "%s: lost key %lu\n", label, (unsigned long)i);
        }
        else
        {
            g_sink += (unsigned long)value[0];
        }
    }
    get_ms = now_ms() - started;
    *ns_per_get = get_ms * 1e6 / (double)KEY_COUNT;

    printf("  %-22s buckets=%5lu longest chain=%4lu  load=%7.2f ms  get=%7.0f ns\n",
           label, (unsigned long)((parser.sections != NULL)
                                      ? parser.sections->key_bucket_count
                                      : 0),
           (unsigned long)*chain_out, load_ms, *ns_per_get);

    ini_parser_free(&parser);
}

int main(void)
{
    char path[] = "/tmp/ini_flood_demo.ini";
    char (*names)[NAME_BUFFER] = (char (*)[NAME_BUFFER])malloc(KEY_COUNT * NAME_BUFFER);
    ini_parser_t probe;
    size_t found = 0;
    unsigned long candidate = 0;
    size_t chain_leaked = 0;
    size_t chain_random = 0;
    double ns_leaked = 0;
    double ns_random = 0;
    FILE *file;
    int failures = 0;

    if (names == NULL)
    {
        fprintf(stderr, "out of memory\n");
        return EXIT_FAILURE;
    }

    printf("hash flooding demo: crafting %d colliding key names\n\n", KEY_COUNT);

    /* The attacker, knowing the key, searches for names that share one bucket. */
    ini_parser_init(&probe);
    ini_parser_set_hash_seed(&probe, g_leaked_key0, g_leaked_key1);

    while (found < KEY_COUNT && candidate < 200000000ul)
    {
        char name[NAME_BUFFER];

        snprintf(name, sizeof(name), "a%lu", candidate);
        candidate++;

        if (hash_string(&probe, name) % TARGET_BUCKETS == 0)
        {
            memcpy(names[found], name, NAME_BUFFER);
            found++;
        }
    }

    ini_parser_free(&probe);

    if (found < KEY_COUNT)
    {
        fprintf(stderr, "could not craft enough colliding names\n");
        free(names);
        return EXIT_FAILURE;
    }

    printf("  scanned %lu candidates to find %lu names sharing a bucket\n\n",
           candidate, (unsigned long)found);

    file = fopen(path, "wb");
    if (file == NULL)
    {
        fprintf(stderr, "cannot write %s\n", path);
        free(names);
        return EXIT_FAILURE;
    }

    fprintf(file, "[s]\n");
    for (candidate = 0; candidate < KEY_COUNT; candidate++)
    {
        fprintf(file, "%s = v\n", names[candidate]);
    }
    fclose(file);

    printf("the same file, parsed twice:\n");
    measure(names, path, "key known to attacker", 0, &chain_leaked, &ns_leaked);
    measure(names, path, "default random key", 1, &chain_random, &ns_random);

    printf("\n  cost of the attack: %.0fx slower lookups (%lu long chain vs %lu)\n",
           (ns_random > 0) ? ns_leaked / ns_random : 0,
           (unsigned long)chain_leaked, (unsigned long)chain_random);

    /* With the secret known the attack collapses the table ... */
    if (chain_leaked < 100)
    {
        fprintf(stderr, "FAIL: the crafted file should produce a long chain (%lu)\n",
                (unsigned long)chain_leaked);
        failures++;
    }

    /* ... and with an unknown secret the very same file is harmless. */
    if (chain_random > 50)
    {
        fprintf(stderr, "FAIL: the default key should spread the crafted names (%lu)\n",
                (unsigned long)chain_random);
        failures++;
    }

    if (ns_leaked < ns_random * 3)
    {
        fprintf(stderr, "FAIL: the crafted file should be measurably slower "
                        "with a known key (%.0f vs %.0f ns)\n", ns_leaked, ns_random);
        failures++;
    }

    printf("  (checksum %lu)\n", (unsigned long)g_sink);
    printf("\n%s\n", (failures == 0) ? "ok" : "FAILED");

    remove(path);
    free(names);
    return (failures == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
