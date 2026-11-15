/*
 *  filename: ini.c
 *  description: A simple INI configuration parser.
 *  author: Rainy101112(https://github.com/Rainy101112)
 *  date: Nov 15th, 2026
 *  license: MIT license
 */

#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ini.h"

/* Starting size of the per-line buffer; it grows as needed, so a line may be
 * arbitrarily long. */
#define INI_LINE_INITIAL_CAPACITY 128

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

static ini_section_t *find_section(const ini_parser_t *parser, const char *name)
{
    ini_section_t *section = parser->sections;

    while (section != NULL)
    {
        if (strcmp(section->name, name) == 0)
        {
            return section;
        }
        section = section->next;
    }

    return NULL;
}

static ini_key_t *find_key(const ini_section_t *section, const char *name)
{
    ini_key_t *key = section->head;

    while (key != NULL)
    {
        if (strcmp(key->name, name) == 0)
        {
            return key;
        }
        key = key->next;
    }

    return NULL;
}

static int append_section(ini_parser_t *parser, const char *name, ini_section_t **section_out)
{
    ini_section_t *section = (ini_section_t *)calloc(1, sizeof(ini_section_t));
    ini_section_t *tail;

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

    if (parser->sections == NULL)
    {
        parser->sections = section;
    }
    else
    {
        tail = parser->sections;
        while (tail->next != NULL)
        {
            tail = tail->next;
        }
        tail->next = section;
    }

    *section_out = section;
    return 0;
}

static int set_key_value(ini_section_t *section, const char *name, const char *value)
{
    ini_key_t *key = find_key(section, name);
    char *value_copy;
    ini_key_t *tail;

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

    if (section->head == NULL)
    {
        section->head = key;
    }
    else
    {
        tail = section->head;
        while (tail->next != NULL)
        {
            tail = tail->next;
        }
        tail->next = key;
    }

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
                append_section(parser, content, &current_section) != 0)
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
                    append_section(parser, INI_DEFAULT_SECTION, &current_section) != 0)
                {
                    status = INI_ERROR_MEMORY;
                    break;
                }
            }

            if (set_key_value(current_section, key, value) != 0)
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

    target_key = find_key(target_section, key);
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

        free(section->name);
        free(section);
        section = next_section;
    }

    parser->sections = NULL;
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
