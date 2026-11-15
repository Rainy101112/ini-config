/*
 *  filename: tests/bench_ini.c
 *  description: Micro benchmark for ini-config (load + lookup).
 *  author: Rainy101112(https://github.com/Rainy101112)
 *  license: MIT license
 *
 *  Only uses the public API, so the same file also builds against older
 *  revisions to compare implementations:
 *
 *      make bench                 # current library, 20000 keys
 *      make bench N=100000
 *      make bench-HEAD            # same benchmark against git HEAD's ini.c
 *
 *  Timing uses clock() (C89, portable) and reports milliseconds. The numbers
 *  are indicative, not a rigorous benchmark: the point is the growth curve as
 *  the key count rises, which is O(n) for a linear scan and O(1) for the hash.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "ini.h"

#define NAME_BUFFER 24

static volatile unsigned long g_sink;

static double now_ms(void)
{
    return (double)clock() * 1000.0 / (double)CLOCKS_PER_SEC;
}

/* Deterministic pseudo random sequence so runs are comparable. */
static unsigned long long g_state = 88172645463325252ull;

static unsigned long long next_random(void)
{
    g_state ^= g_state << 13;
    g_state ^= g_state >> 7;
    g_state ^= g_state << 17;
    return g_state;
}

/* Build the configuration text for `count` keys in one section. */
static char *build_config(size_t count, size_t *out_length)
{
    size_t capacity = count * (NAME_BUFFER * 2 + 8) + 32;
    char *text = (char *)malloc(capacity);
    size_t length = 0;
    size_t i;

    if (text == NULL)
    {
        return NULL;
    }

    length += (size_t)snprintf(text + length, capacity - length, "[bench]\n");
    for (i = 0; i < count; i++)
    {
        length += (size_t)snprintf(text + length, capacity - length,
                                   "key%lu = value%lu\n", (unsigned long)i, (unsigned long)i);
    }

    *out_length = length;
    return text;
}

static int write_config(const char *path, const char *text, size_t length)
{
    FILE *file = fopen(path, "wb");

    if (file == NULL)
    {
        return -1;
    }

    if (fwrite(text, 1, length, file) != length)
    {
        fclose(file);
        return -1;
    }

    fclose(file);
    return 0;
}

int main(int argc, char **argv)
{
    const char *path = "/tmp/ini_bench_config.ini";
    size_t count = 20000;
    char *text;
    char *names;
    size_t *order;
    size_t text_length = 0;
    size_t i;
    ini_parser_t parser;
    double started;
    double load_ms;
    double sequential_ms;
    double random_ms;
    double missing_ms;
    int status;

    if (argc > 1)
    {
        unsigned long parsed = strtoul(argv[1], NULL, 10);

        if (parsed > 0)
        {
            count = (size_t)parsed;
        }
    }

    text = build_config(count, &text_length);
    names = (char *)malloc(count * NAME_BUFFER);
    order = (size_t *)malloc(count * sizeof(*order));

    if (text == NULL || names == NULL || order == NULL)
    {
        fprintf(stderr, "out of memory\n");
        free(text);
        free(names);
        free(order);
        return EXIT_FAILURE;
    }

    for (i = 0; i < count; i++)
    {
        snprintf(names + i * NAME_BUFFER, NAME_BUFFER, "key%lu", (unsigned long)i);
        order[i] = (size_t)(next_random() % count);
    }

    if (write_config(path, text, text_length) != 0)
    {
        fprintf(stderr, "cannot write %s\n", path);
        free(text);
        free(names);
        free(order);
        return EXIT_FAILURE;
    }

    printf("ini-config benchmark: %lu keys, %lu byte config\n",
           (unsigned long)count, (unsigned long)text_length);

    ini_parser_init(&parser);

    started = now_ms();
    status = ini_parser_load(&parser, path);
    load_ms = now_ms() - started;

    if (status != INI_OK)
    {
        fprintf(stderr, "load failed: %s\n", ini_strerror(status));
        ini_parser_free(&parser);
        free(text);
        free(names);
        free(order);
        remove(path);
        return EXIT_FAILURE;
    }

    /* Sequential lookups: worst case for a linear scan. */
    started = now_ms();
    for (i = 0; i < count; i++)
    {
        const char *value = ini_parser_get(&parser, "bench", names + i * NAME_BUFFER);

        if (value != NULL)
        {
            g_sink += (unsigned long)value[0];
        }
    }
    sequential_ms = now_ms() - started;

    /* Random lookups: the realistic access pattern. */
    started = now_ms();
    for (i = 0; i < count; i++)
    {
        const char *value = ini_parser_get(&parser, "bench", names + order[i] * NAME_BUFFER);

        if (value != NULL)
        {
            g_sink += (unsigned long)value[0];
        }
    }
    random_ms = now_ms() - started;

    /* Absent keys must not degrade into a full scan either. */
    started = now_ms();
    for (i = 0; i < count; i++)
    {
        const char *value = ini_parser_get(&parser, "bench", "absent-key");

        if (value != NULL)
        {
            g_sink += (unsigned long)value[0];
        }
    }
    missing_ms = now_ms() - started;

    printf("  load            : %9.3f ms  (%7.1f ns/key)\n",
           load_ms, load_ms * 1e6 / (double)count);
    printf("  get sequential  : %9.3f ms  (%7.1f ns/lookup)\n",
           sequential_ms, sequential_ms * 1e6 / (double)count);
    printf("  get random      : %9.3f ms  (%7.1f ns/lookup)\n",
           random_ms, random_ms * 1e6 / (double)count);
    printf("  get absent      : %9.3f ms  (%7.1f ns/lookup)\n",
           missing_ms, missing_ms * 1e6 / (double)count);
    printf("  (checksum %lu)\n", (unsigned long)g_sink);

    ini_parser_free(&parser);
    free(text);
    free(names);
    free(order);
    remove(path);
    return EXIT_SUCCESS;
}
