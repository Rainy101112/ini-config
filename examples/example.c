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
 * `ini_parser_free`. The default section has the empty name.
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

        if (section->name[0] == '\0')
        {
            printf("[<default>]\n");
        }
        else
        {
            printf("[%s]\n", section->name);
        }

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
    /* Zero initialise, or call ini_parser_init(), before the first load. */
    ini_parser_t parser;
    /* A caller-owned buffer: unlike ini_parser_get() it survives a reload. */
    char greeting[64];
    const char *name;
    int port;
    int status;

    ini_parser_init(&parser);

    status = ini_parser_load(&parser, path);
    if (status != INI_OK)
    {
        fprintf(stderr, "error: %s: %s", path, ini_strerror(status));
        if (parser.last_error_line > 0)
        {
            fprintf(stderr, " (line %d)", parser.last_error_line);
        }
        fprintf(stderr, "\n");
        if (status == INI_ERROR_OPEN)
        {
            fprintf(stderr, "hint: run this from the repository root, or pass a path\n");
        }
        ini_parser_free(&parser);
        return EXIT_FAILURE;
    }

    printf("loaded %s\n\n", path);

    /* Typical use: read a few keys with sensible defaults. */
    name = get_or(&parser, "server", "host", "0.0.0.0");
    port = get_int_or(&parser, "server", "port", 80);

    printf("app name       : %s\n", get_or(&parser, INI_DEFAULT_SECTION, "app_name", "(unnamed)"));
    printf("app version    : %s\n", get_or(&parser, INI_DEFAULT_SECTION, "version", "0"));
    printf("server address : %s:%d\n", name, port);
    printf("log level      : %s\n", get_or(&parser, "logging", "level", "warn"));
    printf("log file       : %s\n", get_or(&parser, "logging", "file", "(stderr)"));
    printf("log rotation   : %s\n", get_or(&parser, "logging", "rotate", "(off)"));

    /* Copy a value out when you need to keep it beyond the parser's lifetime,
     * or when it comes from an untrusted file and you do not want to hold a
     * pointer into it. */
    status = ini_parser_get_copy(&parser, "server", "greeting", greeting, sizeof(greeting));
    if (status == INI_OK)
    {
        printf("server greeting: %s\n", greeting);
    }
    else
    {
        printf("server greeting: %s (%s)\n", "(none)", ini_strerror(status));
    }

    /* Absent section or key simply yields NULL; ask for the default yourself. */
    printf("missing key    : %s\n\n",
           get_or(&parser, "server", "does-not-exist", "(default)"));

    dump(&parser);

    ini_parser_free(&parser);
    return EXIT_SUCCESS;
}
