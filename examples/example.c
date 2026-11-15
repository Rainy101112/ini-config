/*
 *  filename: examples/example.c
 *  description: Usage example for the ini-config parser.
 *  author: Rainy101112(https://github.com/Rainy101112)
 *  license: MIT license
 *
 *  Build and run from the repository root:
 *
 *      make examples
 *      ./build/example                 # uses examples/example.ini
 *      ./build/example /path/to/x.ini  # uses your own file
 */

#include <stdio.h>
#include <stdlib.h>

#include "ini.h"

/* Look a key up and fall back to a default when it is absent. */
static const char *get_or(const ini_parser_t *parser, const char *section,
                          const char *key, const char *fallback)
{
    const char *value = ini_parser_get(parser, section, key);

    return (value != NULL) ? value : fallback;
}

/* Read a value as an integer, falling back on absence or bad syntax. */
static int get_int_or(const ini_parser_t *parser, const char *section,
                      const char *key, int fallback)
{
    const char *value = ini_parser_get(parser, section, key);
    char *end = NULL;
    long parsed;

    if (value == NULL || *value == '\0')
    {
        return fallback;
    }

    parsed = strtol(value, &end, 10);
    if (end == value || (end != NULL && *end != '\0'))
    {
        fprintf(stderr, "warning: [%s] %s = \"%s\" is not an integer, using %d\n",
                section, key, value, fallback);
        return fallback;
    }

    return (int)parsed;
}

/*
 * The structs are public, so the whole file can be dumped without extra API.
 * `ini_parser_get` returns a pointer into the parser: use it before
 * `ini_parser_free`.
 */
static void dump(const ini_parser_t *parser)
{
    const ini_section_t *section = parser->sections;

    printf("--- dump ---\n");
    if (section == NULL)
    {
        printf("(no sections)\n");
        return;
    }

    for (; section != NULL; section = section->next)
    {
        const ini_key_t *key = section->head;

        printf("[%s]\n", section->name);
        if (key == NULL)
        {
            printf("    (no keys)\n");
        }

        for (; key != NULL; key = key->next)
        {
            printf("    %s = \"%s\"\n", key->name, key->value);
        }
    }
}

int main(int argc, char **argv)
{
    const char *path = (argc > 1) ? argv[1] : "examples/example.ini";
    /* The parser must be zero initialised: ini_parser_load frees it first. */
    ini_parser_t parser = {0};
    const char *name;
    int port;

    if (ini_parser_load(&parser, path) != 0)
    {
        fprintf(stderr, "error: could not parse \"%s\"\n", path);
        fprintf(stderr, "hint: run this from the repository root, or pass a path\n");
        return EXIT_FAILURE;
    }

    printf("loaded %s\n\n", path);

    /* Typical use: read a few keys with sensible defaults. */
    name = get_or(&parser, "server", "host", "0.0.0.0");
    port = get_int_or(&parser, "server", "port", 80);

    printf("server address : %s:%d\n", name, port);
    printf("server greeting: %s\n", get_or(&parser, "server", "greeting", "(none)"));
    printf("log level      : %s\n", get_or(&parser, "logging", "level", "warn"));
    printf("log file       : %s\n", get_or(&parser, "logging", "file", "(stderr)"));
    printf("log rotation   : %s\n", get_or(&parser, "logging", "rotate", "(off)"));

    /* Absent section or key simply yields NULL; ask for the default yourself. */
    printf("missing key    : %s\n\n",
           get_or(&parser, "server", "does-not-exist", "(default)"));

    dump(&parser);

    ini_parser_free(&parser);
    return EXIT_SUCCESS;
}
