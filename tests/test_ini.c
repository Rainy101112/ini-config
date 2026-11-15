/*
 *  filename: tests/test_ini.c
 *  description: Self-contained test suite for the ini-config parser.
 *  author: Rainy101112(https://github.com/Rainy101112)
 *  license: MIT license
 *
 *  No external test framework: the harness below is ~60 lines so that the
 *  project keeps zero dependencies. Run with:
 *
 *      make test
 *      ./build/test_ini            # uses ./ as scratch dir
 *      INI_TEST_TMPDIR=/tmp ./build/test_ini
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ini.h"

/* ------------------------------------------------------------------ */
/* tiny test harness                                                   */
/* ------------------------------------------------------------------ */

static int g_checks;
static int g_failures;
static const char *g_tmpdir = ".";

#define CHECK(cond)                                                            \
    do                                                                         \
    {                                                                          \
        g_checks++;                                                            \
        if (!(cond))                                                           \
        {                                                                      \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
            g_failures++;                                                      \
        }                                                                      \
    } while (0)

static void check_str(const char *actual, const char *expected,
                      const char *what, int line)
{
    g_checks++;
    if (actual == NULL && expected == NULL)
    {
        return;
    }

    if (actual == NULL || expected == NULL || strcmp(actual, expected) != 0)
    {
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, line, what);
        fprintf(stderr, "  expected: \"%s\"\n", (expected != NULL) ? expected : "(null)");
        fprintf(stderr, "  actual  : \"%s\"\n", (actual != NULL) ? actual : "(null)");
        g_failures++;
    }
}

#define CHECK_STR(actual, expected)                                            \
    check_str((actual), (expected), #actual, __LINE__)

#define CHECK_EQ(actual, expected)                                             \
    do                                                                         \
    {                                                                          \
        long a_ = (long)(actual);                                              \
        long e_ = (long)(expected);                                            \
        g_checks++;                                                            \
        if (a_ != e_)                                                          \
        {                                                                      \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #actual);  \
            fprintf(stderr, "  expected: %ld\n  actual  : %ld\n", e_, a_);     \
            g_failures++;                                                      \
        }                                                                      \
    } while (0)

/* Write `contents` to <tmpdir>/<name>; returns a malloc'ed path or NULL. */
static char *write_file(const char *name, const char *contents)
{
    char path[512];
    size_t length = strlen(contents);
    FILE *file;
    char *copy;
    int written;

    written = snprintf(path, sizeof(path), "%s/%s", g_tmpdir, name);
    if (written < 0 || (size_t)written >= sizeof(path))
    {
        fprintf(stderr, "FAIL: scratch path too long\n");
        g_failures++;
        return NULL;
    }

    file = fopen(path, "wb");
    if (file == NULL)
    {
        fprintf(stderr, "FAIL: cannot create %s\n", path);
        g_failures++;
        return NULL;
    }

    if (fwrite(contents, 1, length, file) != length)
    {
        fclose(file);
        fprintf(stderr, "FAIL: cannot write %s\n", path);
        g_failures++;
        return NULL;
    }

    fclose(file);

    copy = (char *)malloc(strlen(path) + 1);
    if (copy == NULL)
    {
        fprintf(stderr, "FAIL: out of memory\n");
        g_failures++;
        return NULL;
    }

    memcpy(copy, path, strlen(path) + 1);
    return copy;
}

/* Load a config from an inline string. Returns 0 on success, negative on error. */
static int load_string(ini_parser_t *parser, const char *name, const char *contents)
{
    char *path = write_file(name, contents);
    int result;

    if (path == NULL)
    {
        return INI_ERROR;
    }

    result = ini_parser_load(parser, path);
    remove(path);
    free(path);
    return result;
}

/* Growing string buffer used to generate large configurations. Appends `text`
 * and returns the (possibly reallocated) buffer, or NULL after freeing it. */
static char *sbuf_append(char **buffer, size_t *length, size_t *capacity, const char *text)
{
    size_t needed = strlen(text);

    if (*length + needed + 1 > *capacity)
    {
        size_t wanted = (*capacity == 0) ? 256 : *capacity;
        char *grown;

        while (wanted < *length + needed + 1)
        {
            wanted *= 2;
        }

        grown = (char *)realloc(*buffer, wanted);
        if (grown == NULL)
        {
            free(*buffer);
            *buffer = NULL;
            *length = 0;
            *capacity = 0;
            return NULL;
        }

        *buffer = grown;
        *capacity = wanted;
    }

    memcpy(*buffer + *length, text, needed + 1);
    *length += needed;
    return *buffer;
}

static char *sbuf_append_fmt(char **buffer, size_t *length, size_t *capacity,
                             const char *format, ...)
{
    char temp[128];
    va_list args;
    int written;

    va_start(args, format);
    written = vsnprintf(temp, sizeof(temp), format, args);
    va_end(args);

    if (written < 0 || (size_t)written >= sizeof(temp))
    {
        return NULL;
    }

    return sbuf_append(buffer, length, capacity, temp);
}

/* ------------------------------------------------------------------ */
/* parsing basics                                                      */
/* ------------------------------------------------------------------ */

static void test_basic(void)
{
    ini_parser_t parser;

    ini_parser_init(&parser);

    CHECK_EQ(load_string(&parser,
                         "basic.ini",
                         "; leading comment\n"
                         "# hash comment\n"
                         "\n"
                         "[server]\n"
                         "host = localhost\n"
                         "port = 8080\n"
                         "\n"
                         "[database]\n"
                         "user = admin\n"
                         "password = s3cr3t\n"),
             0);

    CHECK_STR(ini_parser_get(&parser, "server", "host"), "localhost");
    CHECK_STR(ini_parser_get(&parser, "server", "port"), "8080");
    CHECK_STR(ini_parser_get(&parser, "database", "user"), "admin");
    CHECK_STR(ini_parser_get(&parser, "database", "password"), "s3cr3t");

    /* unknown key, unknown section, mismatched pair */
    CHECK(ini_parser_get(&parser, "server", "password") == NULL);
    CHECK(ini_parser_get(&parser, "nope", "host") == NULL);
    CHECK(ini_parser_get(&parser, "nope", "nope") == NULL);

    ini_parser_free(&parser);
}

static void test_whitespace_and_tabs(void)
{
    ini_parser_t parser;

    ini_parser_init(&parser);

    CHECK_EQ(load_string(&parser,
                         "ws.ini",
                         "   [  padded section  ]   \n"
                         "\tindented = value\n"
                         "   spaced key   =   spaced value   \n"),
             0);

    CHECK_STR(ini_parser_get(&parser, "padded section", "indented"), "value");
    CHECK_STR(ini_parser_get(&parser, "padded section", "spaced key"), "spaced value");

    ini_parser_free(&parser);
}

static void test_empty_values(void)
{
    ini_parser_t parser;

    ini_parser_init(&parser);

    CHECK_EQ(load_string(&parser,
                         "empty.ini",
                         "[s]\n"
                         "empty =\n"
                         "spaces =    \n"
                         "equals = = value\n"),
             0);

    CHECK_STR(ini_parser_get(&parser, "s", "empty"), "");
    CHECK_STR(ini_parser_get(&parser, "s", "spaces"), "");
    /* Only the first '=' splits the line. */
    CHECK_STR(ini_parser_get(&parser, "s", "equals"), "= value");

    ini_parser_free(&parser);
}

static void test_equals_in_value(void)
{
    ini_parser_t parser;

    ini_parser_init(&parser);

    CHECK_EQ(load_string(&parser, "equals.ini", "[s]\nurl = http://x/?a=1&b=2\n"), 0);
    CHECK_STR(ini_parser_get(&parser, "s", "url"), "http://x/?a=1&b=2");

    ini_parser_free(&parser);
}

/* ------------------------------------------------------------------ */
/* comments                                                            */
/* ------------------------------------------------------------------ */

static void test_comments(void)
{
    ini_parser_t parser;

    ini_parser_init(&parser);

    CHECK_EQ(load_string(&parser,
                         "comments.ini",
                         "[s]\n"
                         "a = value ; trailing\n"
                         "b = value\t# tab before hash\n"
                         "c = a#b\n"
                         "d = x;y\n"
                         "e = # only a comment\n"
                         "f = 1\n"
                         "   ; a comment line inside a section\n"
                         "g = 2\n"
                         "[commented]   ; header comment\n"
                         "h = 3\n"),
             0);

    /* A comment marker preceded by whitespace starts an inline comment. */
    CHECK_STR(ini_parser_get(&parser, "s", "a"), "value");
    CHECK_STR(ini_parser_get(&parser, "s", "b"), "value");
    /* Without preceding whitespace it is part of the value. */
    CHECK_STR(ini_parser_get(&parser, "s", "c"), "a#b");
    CHECK_STR(ini_parser_get(&parser, "s", "d"), "x;y");
    /* Marker preceded by whitespace eats the whole value. */
    CHECK_STR(ini_parser_get(&parser, "s", "e"), "");
    CHECK_STR(ini_parser_get(&parser, "s", "f"), "1");
    CHECK_STR(ini_parser_get(&parser, "s", "g"), "2");
    /* Comment after a section header is allowed. */
    CHECK_STR(ini_parser_get(&parser, "commented", "h"), "3");

    ini_parser_free(&parser);
}

/* ------------------------------------------------------------------ */
/* quoted values                                                       */
/* ------------------------------------------------------------------ */

static void test_quoted_values(void)
{
    ini_parser_t parser;

    ini_parser_init(&parser);

    CHECK_EQ(load_string(&parser,
                         "quotes.ini",
                         "[s]\n"
                         "hash = \"a # b\"\n"
                         "semi = 'x; y'\n"
                         "apostrophe = it's fine\n"
                         "bare = un\"quoted\n"
                         "empty = \"\"\n"
                         "inner = \"  keep  \"\n"
                         "trailing = \"a\" ; note\n"
                         "tight = \"a\"#b\n"
                         "nospace=\"v\"\n"),
             0);

    /* Comment markers inside quotes are literal. */
    CHECK_STR(ini_parser_get(&parser, "s", "hash"), "a # b");
    CHECK_STR(ini_parser_get(&parser, "s", "semi"), "x; y");
    /* An apostrophe that does not start a token is literal. */
    CHECK_STR(ini_parser_get(&parser, "s", "apostrophe"), "it's fine");
    /* Quotes in the middle of a value are left alone. */
    CHECK_STR(ini_parser_get(&parser, "s", "bare"), "un\"quoted");
    CHECK_STR(ini_parser_get(&parser, "s", "empty"), "");
    /* Inner whitespace survives because it is inside the quotes. */
    CHECK_STR(ini_parser_get(&parser, "s", "inner"), "  keep  ");
    /* A comment after the closing quote is stripped. */
    CHECK_STR(ini_parser_get(&parser, "s", "trailing"), "a");
    /* No whitespace before '#': it stays, and the value is not fully quoted. */
    CHECK_STR(ini_parser_get(&parser, "s", "tight"), "\"a\"#b");
    CHECK_STR(ini_parser_get(&parser, "s", "nospace"), "v");

    ini_parser_free(&parser);
}

/* ------------------------------------------------------------------ */
/* overriding, merging, default section                                */
/* ------------------------------------------------------------------ */

static void test_duplicate_key_last_wins(void)
{
    ini_parser_t parser;

    ini_parser_init(&parser);

    CHECK_EQ(load_string(&parser,
                         "dupkey.ini",
                         "[s]\n"
                         "k = first\n"
                         "other = 1\n"
                         "k = second\n"),
             0);

    CHECK_STR(ini_parser_get(&parser, "s", "k"), "second");
    CHECK_STR(ini_parser_get(&parser, "s", "other"), "1");

    ini_parser_free(&parser);
}

static void test_duplicate_section_merges(void)
{
    ini_parser_t parser;
    const ini_section_t *section;
    int count = 0;

    ini_parser_init(&parser);

    CHECK_EQ(load_string(&parser,
                         "dupsection.ini",
                         "[a]\nx = 1\n"
                         "[b]\ny = 2\n"
                         "[a]\nz = 3\n"),
             0);

    CHECK_STR(ini_parser_get(&parser, "a", "x"), "1");
    CHECK_STR(ini_parser_get(&parser, "a", "z"), "3");

    /* The repeated header must not create a second node. */
    for (section = parser.sections; section != NULL; section = section->next)
    {
        count++;
    }
    CHECK_EQ(count, 2);

    ini_parser_free(&parser);
}

static void test_default_section(void)
{
    ini_parser_t parser;
    const ini_section_t *section;
    int count = 0;

    ini_parser_init(&parser);

    /* Keys before the first header land in INI_DEFAULT_SECTION. */
    CHECK_EQ(load_string(&parser,
                         "default.ini",
                         "top = 1\n"
                         "other = 2\n"
                         "[s]\n"
                         "a = 3\n"),
             INI_OK);

    CHECK_STR(ini_parser_get(&parser, INI_DEFAULT_SECTION, "top"), "1");
    CHECK_STR(ini_parser_get(&parser, INI_DEFAULT_SECTION, "other"), "2");
    CHECK_STR(ini_parser_get(&parser, "s", "a"), "3");

    /* The default section is created once, and it comes first. */
    CHECK(parser.sections != NULL);
    if (parser.sections != NULL)
    {
        CHECK_STR(parser.sections->name, INI_DEFAULT_SECTION);
    }

    for (section = parser.sections; section != NULL; section = section->next)
    {
        count++;
    }
    CHECK_EQ(count, 2);

    ini_parser_free(&parser);
}

static void test_default_section_only(void)
{
    ini_parser_t parser;

    ini_parser_init(&parser);

    /* A file without any header is now valid. */
    CHECK_EQ(load_string(&parser, "noheader.ini", "a = 1\nb = 2\n"), INI_OK);
    CHECK_STR(ini_parser_get(&parser, INI_DEFAULT_SECTION, "a"), "1");
    CHECK_STR(ini_parser_get(&parser, INI_DEFAULT_SECTION, "b"), "2");

    ini_parser_free(&parser);
}

/* ------------------------------------------------------------------ */
/* line endings and line length                                        */
/* ------------------------------------------------------------------ */

static void test_crlf_and_no_final_newline(void)
{
    ini_parser_t parser;

    ini_parser_init(&parser);

    CHECK_EQ(load_string(&parser,
                         "crlf.ini",
                         "[s]\r\n"
                         "a = 1\r\n"
                         "b = 2"),
             INI_OK);

    /* trim() drops the '\r' as trailing whitespace. */
    CHECK_STR(ini_parser_get(&parser, "s", "a"), "1");
    CHECK_STR(ini_parser_get(&parser, "s", "b"), "2");

    ini_parser_free(&parser);
}

static void test_long_lines_are_supported(void)
{
    static const size_t lengths[] = {126, 127, 128, 1023, 1024, 2000, 5000};
    size_t i;

    for (i = 0; i < sizeof(lengths) / sizeof(lengths[0]); i++)
    {
        ini_parser_t parser;
        char name[32];
        char *contents;
        char *value;
        size_t length = lengths[i];
        size_t j;
        size_t ok = 1;

        ini_parser_init(&parser);

        /* "[s]\nkey = " + length 'a's + "\n" */
        contents = (char *)malloc(10 + length + 2);
        if (contents == NULL)
        {
            CHECK(!"out of memory");
            return;
        }

        memcpy(contents, "[s]\nkey = ", 10);
        for (j = 0; j < length; j++)
        {
            contents[10 + j] = 'a';
        }
        contents[10 + length] = '\n';
        contents[11 + length] = '\0';

        snprintf(name, sizeof(name), "long_%lu.ini", (unsigned long)length);
        CHECK_EQ(load_string(&parser, name, contents), INI_OK);

        value = (char *)ini_parser_get(&parser, "s", "key");
        if (value == NULL || strlen(value) != length)
        {
            ok = 0;
        }
        else
        {
            for (j = 0; j < length; j++)
            {
                if (value[j] != 'a')
                {
                    ok = 0;
                    break;
                }
            }
        }

        g_checks++;
        if (!ok)
        {
            fprintf(stderr, "FAIL %s:%d: long line of %lu bytes was truncated\n",
                    __FILE__, __LINE__, (unsigned long)length);
            g_failures++;
        }

        ini_parser_free(&parser);
        free(contents);
    }
}

static void test_many_long_lines(void)
{
    ini_parser_t parser;
    char *contents;
    const size_t count = 50;
    const size_t value_length = 1500;
    const size_t capacity = count * (value_length + 32) + 64;
    size_t offset = 0;
    size_t i;
    size_t ok = 1;

    ini_parser_init(&parser);

    contents = (char *)malloc(capacity);
    if (contents == NULL)
    {
        CHECK(!"out of memory");
        return;
    }

    offset += (size_t)snprintf(contents + offset, capacity - offset, "[s]\n");
    for (i = 0; i < count; i++)
    {
        size_t j;

        offset += (size_t)snprintf(contents + offset, capacity - offset,
                                   "key%lu = ", (unsigned long)i);
        for (j = 0; j < value_length; j++)
        {
            contents[offset++] = (char)('a' + (i % 26));
        }
        contents[offset++] = '\n';
    }
    contents[offset] = '\0';

    CHECK_EQ(load_string(&parser, "manylong.ini", contents), INI_OK);

    for (i = 0; i < count; i++)
    {
        char key[32];
        const char *value;

        snprintf(key, sizeof(key), "key%lu", (unsigned long)i);
        value = ini_parser_get(&parser, "s", key);
        if (value == NULL || strlen(value) != value_length ||
            value[0] != (char)('a' + (i % 26)))
        {
            ok = 0;
        }
    }

    g_checks++;
    if (!ok)
    {
        fprintf(stderr, "FAIL %s:%d: long multi-line file misparsed\n", __FILE__, __LINE__);
        g_failures++;
    }

    ini_parser_free(&parser);
    free(contents);
}

static void test_long_line_with_comment(void)
{
    ini_parser_t parser;
    char *contents;
    size_t offset = 0;
    size_t i;

    ini_parser_init(&parser);

    contents = (char *)malloc(2100);
    if (contents == NULL)
    {
        CHECK(!"out of memory");
        return;
    }

    offset += (size_t)snprintf(contents + offset, 2100 - offset, "[s]\nkey = ");
    for (i = 0; i < 2000; i++)
    {
        contents[offset++] = 'b';
    }
    contents[offset++] = ' ';
    contents[offset++] = ';';
    contents[offset++] = ' ';
    contents[offset++] = 'c';
    contents[offset++] = '\n';
    contents[offset] = '\0';

    CHECK_EQ(load_string(&parser, "longcomment.ini", contents), INI_OK);
    CHECK(ini_parser_get(&parser, "s", "key") != NULL);
    if (ini_parser_get(&parser, "s", "key") != NULL)
    {
        CHECK_EQ(strlen(ini_parser_get(&parser, "s", "key")), 2000);
    }

    ini_parser_free(&parser);
    free(contents);
}

/* ------------------------------------------------------------------ */
/* error handling                                                      */
/* ------------------------------------------------------------------ */

static void test_missing_file(void)
{
    ini_parser_t parser;

    ini_parser_init(&parser);

    CHECK_EQ(ini_parser_load(&parser, "this/file/does/not/exist.ini"), INI_ERROR_OPEN);
    CHECK(parser.sections == NULL);
    CHECK_EQ(parser.last_error, INI_ERROR_OPEN);
    CHECK(ini_parser_get(&parser, "s", "k") == NULL);

    ini_parser_free(&parser);
}

static void test_syntax_errors(void)
{
    static const struct
    {
        const char *contents;
        int line;
    } cases[] = {
        {"[unclosed\nkey = 1\n", 1},         /* missing ']'          */
        {"[s]\n[]\nkey = 1\n", 2},           /* empty section name   */
        {"[s]\nnot a key value pair\n", 2},  /* line without '='     */
        {"[s]\n = no key\n", 2},             /* empty key            */
        {"[s]\nk = v\n[broken\n", 3},        /* error on the last line */
    };
    size_t i;
    char name[32];

    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
    {
        ini_parser_t parser;

        ini_parser_init(&parser);

        snprintf(name, sizeof(name), "bad_%lu.ini", (unsigned long)i);
        CHECK_EQ(load_string(&parser, name, cases[i].contents), INI_ERROR_SYNTAX);
        CHECK_EQ(parser.last_error, INI_ERROR_SYNTAX);
        CHECK_EQ(parser.last_error_line, cases[i].line);

        /* A failed load leaves the parser empty and still frees cleanly. */
        CHECK(parser.sections == NULL);
        ini_parser_free(&parser);
    }
}

static void test_error_state_reset_on_success(void)
{
    ini_parser_t parser;

    ini_parser_init(&parser);

    CHECK_EQ(load_string(&parser, "fail_first.ini", "[broken\n"), INI_ERROR_SYNTAX);
    CHECK_EQ(parser.last_error, INI_ERROR_SYNTAX);
    CHECK_EQ(parser.last_error_line, 1);

    CHECK_EQ(load_string(&parser, "then_ok.ini", "[s]\nk = v\n"), INI_OK);
    CHECK_EQ(parser.last_error, INI_OK);
    CHECK_EQ(parser.last_error_line, 0);
    CHECK_STR(ini_parser_get(&parser, "s", "k"), "v");

    ini_parser_free(&parser);
}

static void test_null_arguments(void)
{
    ini_parser_t parser;

    ini_parser_init(&parser);

    CHECK_EQ(ini_parser_load(NULL, "x.ini"), INI_ERROR_ARGUMENT);
    CHECK_EQ(ini_parser_load(&parser, NULL), INI_ERROR_ARGUMENT);

    CHECK(ini_parser_get(NULL, "s", "k") == NULL);
    CHECK(ini_parser_get(&parser, NULL, "k") == NULL);
    CHECK(ini_parser_get(&parser, "s", NULL) == NULL);

    ini_parser_init(NULL); /* must not crash */
    ini_parser_free(NULL); /* must not crash */

    ini_parser_free(&parser);
}

static void test_strerror(void)
{
    static const int statuses[] = {
        INI_OK,        INI_ERROR,           INI_ERROR_ARGUMENT, INI_ERROR_OPEN,
        INI_ERROR_READ, INI_ERROR_SYNTAX,   INI_ERROR_MEMORY,   INI_ERROR_NOT_FOUND,
        INI_ERROR_TOO_SMALL, -999
    };
    size_t i;

    for (i = 0; i < sizeof(statuses) / sizeof(statuses[0]); i++)
    {
        const char *message = ini_strerror(statuses[i]);

        CHECK(message != NULL);
        if (message != NULL)
        {
            CHECK(message[0] != '\0');
        }
    }

    CHECK_STR(ini_strerror(INI_OK), "success");
}

/* ------------------------------------------------------------------ */
/* get_copy                                                            */
/* ------------------------------------------------------------------ */

static void test_get_copy(void)
{
    ini_parser_t parser;
    char buffer[32];

    ini_parser_init(&parser);

    CHECK_EQ(load_string(&parser, "copy.ini", "[s]\nname = value\nempty =\n"), INI_OK);

    memset(buffer, 'X', sizeof(buffer));
    CHECK_EQ(ini_parser_get_copy(&parser, "s", "name", buffer, sizeof(buffer)), INI_OK);
    CHECK_STR(buffer, "value");

    /* Exact fit: length + 1 == out_size. */
    memset(buffer, 'X', sizeof(buffer));
    CHECK_EQ(ini_parser_get_copy(&parser, "s", "name", buffer, 6), INI_OK);
    CHECK_STR(buffer, "value");

    /* One byte short: truncated and NUL terminated. */
    memset(buffer, 'X', sizeof(buffer));
    CHECK_EQ(ini_parser_get_copy(&parser, "s", "name", buffer, 4), INI_ERROR_TOO_SMALL);
    CHECK_STR(buffer, "val");

    /* Empty value needs a single byte. */
    CHECK_EQ(ini_parser_get_copy(&parser, "s", "empty", buffer, 1), INI_OK);
    CHECK_STR(buffer, "");

    /* Missing key clears the destination. */
    memset(buffer, 'X', sizeof(buffer));
    CHECK_EQ(ini_parser_get_copy(&parser, "s", "missing", buffer, sizeof(buffer)),
             INI_ERROR_NOT_FOUND);
    CHECK_STR(buffer, "");

    CHECK_EQ(ini_parser_get_copy(&parser, "s", "name", NULL, 8), INI_ERROR_ARGUMENT);
    CHECK_EQ(ini_parser_get_copy(&parser, "s", "name", buffer, 0), INI_ERROR_ARGUMENT);

    ini_parser_free(&parser);
}

/* ------------------------------------------------------------------ */
/* hash index: behaviour at scale and insertion order                  */
/* ------------------------------------------------------------------ */

/*
 * Every section/key must stay reachable after the index has grown and
 * rehashed several times, and file order must not be disturbed by the index.
 */
static void test_large_section_table(void)
{
    enum
    {
        SECTION_COUNT = 1000
    };
    ini_parser_t parser;
    char *contents = NULL;
    size_t length = 0;
    size_t capacity = 0;
    const ini_section_t *section;
    int ok = 1;
    int count = 0;
    int i;

    ini_parser_init(&parser);

    for (i = 0; i < (int)SECTION_COUNT; i++)
    {
        if (sbuf_append_fmt(&contents, &length, &capacity,
                            "[section-%d]\nvalue = %d\n", i, i) == NULL)
        {
            break;
        }
    }

    if (contents == NULL)
    {
        CHECK(!"out of memory");
        return;
    }

    CHECK_EQ(load_string(&parser, "largesections.ini", contents), INI_OK);
    free(contents);

    for (i = 0; i < (int)SECTION_COUNT; i++)
    {
        char section_name[32];
        char expected[32];
        const char *value;

        snprintf(section_name, sizeof(section_name), "section-%d", i);
        snprintf(expected, sizeof(expected), "%d", i);

        value = ini_parser_get(&parser, section_name, "value");
        if (value == NULL || strcmp(value, expected) != 0)
        {
            ok = 0;
        }
    }

    for (section = parser.sections; section != NULL; section = section->next)
    {
        char expected[32];

        snprintf(expected, sizeof(expected), "section-%d", count);
        if (strcmp(section->name, expected) != 0 || section->head == NULL)
        {
            ok = 0;
        }
        count++;
    }

    g_checks++;
    if (!ok || count != (int)SECTION_COUNT)
    {
        fprintf(stderr, "FAIL %s:%d: large section table: ok=%d count=%d\n",
                __FILE__, __LINE__, ok, count);
        g_failures++;
    }

    ini_parser_free(&parser);
}

static void test_large_key_table(void)
{
    enum
    {
        KEY_COUNT = 5000
    };
    ini_parser_t parser;
    char *contents = NULL;
    size_t length = 0;
    size_t capacity = 0;
    const ini_key_t *key;
    int ok = 1;
    int count = 0;
    int i;

    ini_parser_init(&parser);

    if (sbuf_append(&contents, &length, &capacity, "[big]\n") == NULL)
    {
        CHECK(!"out of memory");
        return;
    }

    for (i = 0; i < (int)KEY_COUNT; i++)
    {
        if (sbuf_append_fmt(&contents, &length, &capacity, "key%d = value-%d\n", i, i) == NULL)
        {
            break;
        }
    }

    if (contents == NULL)
    {
        CHECK(!"out of memory");
        return;
    }

    CHECK_EQ(load_string(&parser, "largekeys.ini", contents), INI_OK);
    free(contents);

    for (i = 0; i < (int)KEY_COUNT; i++)
    {
        char name[32];
        char expected[32];
        const char *value;

        snprintf(name, sizeof(name), "key%d", i);
        snprintf(expected, sizeof(expected), "value-%d", i);

        value = ini_parser_get(&parser, "big", name);
        if (value == NULL || strcmp(value, expected) != 0)
        {
            ok = 0;
        }
    }

    /* Names that were never inserted must not be found. */
    CHECK(ini_parser_get(&parser, "big", "key0x") == NULL);
    CHECK(ini_parser_get(&parser, "big", "") == NULL);
    CHECK(ini_parser_get(&parser, "big", "KEY0") == NULL);

    /* File order is preserved independently of the bucket chains. */
    CHECK(parser.sections != NULL);
    if (parser.sections != NULL)
    {
        for (key = parser.sections->head; key != NULL; key = key->next)
        {
            char expected[32];

            snprintf(expected, sizeof(expected), "key%d", count);
            if (strcmp(key->name, expected) != 0)
            {
                ok = 0;
            }
            count++;
        }
    }

    g_checks++;
    if (!ok || count != (int)KEY_COUNT)
    {
        fprintf(stderr, "FAIL %s:%d: large key table: ok=%d count=%d\n",
                __FILE__, __LINE__, ok, count);
        g_failures++;
    }

    ini_parser_free(&parser);
}

/* Many sections with many keys each: both indexes grow in the same file. */
static void test_mixed_large_table(void)
{
    enum
    {
        SECTION_COUNT = 200,
        KEYS_PER_SECTION = 25
    };
    ini_parser_t parser;
    char *contents = NULL;
    size_t length = 0;
    size_t capacity = 0;
    int ok = 1;
    int s;
    int k;

    ini_parser_init(&parser);

    for (s = 0; s < (int)SECTION_COUNT; s++)
    {
        if (sbuf_append_fmt(&contents, &length, &capacity, "[s%d]\n", s) == NULL)
        {
            break;
        }
        for (k = 0; k < (int)KEYS_PER_SECTION; k++)
        {
            if (sbuf_append_fmt(&contents, &length, &capacity, "k%d = v%d-%d\n", k, s, k) == NULL)
            {
                break;
            }
        }
    }

    if (contents == NULL)
    {
        CHECK(!"out of memory");
        return;
    }

    CHECK_EQ(load_string(&parser, "mixed.ini", contents), INI_OK);
    free(contents);

    for (s = 0; s < (int)SECTION_COUNT; s++)
    {
        char section_name[32];

        snprintf(section_name, sizeof(section_name), "s%d", s);
        for (k = 0; k < (int)KEYS_PER_SECTION; k++)
        {
            char key_name[32];
            char expected[32];
            const char *value;

            snprintf(key_name, sizeof(key_name), "k%d", k);
            snprintf(expected, sizeof(expected), "v%d-%d", s, k);

            value = ini_parser_get(&parser, section_name, key_name);
            if (value == NULL || strcmp(value, expected) != 0)
            {
                ok = 0;
            }
        }
    }

    /* No key from one section may leak into another. */
    CHECK(ini_parser_get(&parser, "s0", "k1") != NULL);
    if (ini_parser_get(&parser, "s0", "k1") != NULL)
    {
        CHECK_STR(ini_parser_get(&parser, "s0", "k1"), "v0-1");
    }
    CHECK(ini_parser_get(&parser, "s999", "k0") == NULL);

    g_checks++;
    if (!ok)
    {
        fprintf(stderr, "FAIL %s:%d: mixed large table misparsed\n", __FILE__, __LINE__);
        g_failures++;
    }

    ini_parser_free(&parser);
}

/* Overriding an existing key must work after the index grew, and must keep the
 * key's original position in file order. */
static void test_duplicate_key_after_growth(void)
{
    ini_parser_t parser;
    char *contents = NULL;
    size_t length = 0;
    size_t capacity = 0;
    const ini_key_t *key;
    int ok = 1;
    int position = 0;
    int first_position = -1;
    int i;

    ini_parser_init(&parser);

    if (sbuf_append(&contents, &length, &capacity, "[s]\nearly = original\n") == NULL)
    {
        CHECK(!"out of memory");
        return;
    }

    for (i = 0; i < 200; i++)
    {
        if (sbuf_append_fmt(&contents, &length, &capacity, "fill%d = x\n", i) == NULL)
        {
            break;
        }
    }

    if (contents == NULL ||
        sbuf_append(&contents, &length, &capacity, "early = overwritten\n") == NULL)
    {
        CHECK(!"out of memory");
        free(contents);
        return;
    }

    CHECK_EQ(load_string(&parser, "growthdup.ini", contents), INI_OK);
    free(contents);

    CHECK_STR(ini_parser_get(&parser, "s", "early"), "overwritten");

    if (parser.sections != NULL)
    {
        for (key = parser.sections->head; key != NULL; key = key->next)
        {
            if (strcmp(key->name, "early") == 0)
            {
                first_position = position;
            }
            else if (strcmp(key->name, "fill0") == 0 && position != 1)
            {
                ok = 0;
            }
            position++;
        }
    }

    g_checks++;
    if (!ok || first_position != 0 || position != 201)
    {
        fprintf(stderr, "FAIL %s:%d: duplicate after growth: ok=%d first=%d count=%d\n",
                __FILE__, __LINE__, ok, first_position, position);
        g_failures++;
    }

    ini_parser_free(&parser);
}

/* Reloading a large file must reset both indexes completely. */
static void test_reload_resets_index(void)
{
    ini_parser_t parser;
    char *contents = NULL;
    size_t length = 0;
    size_t capacity = 0;
    int i;
    int ok = 1;

    ini_parser_init(&parser);

    for (i = 0; i < 300; i++)
    {
        if (sbuf_append_fmt(&contents, &length, &capacity, "[bulk%d]\nk = %d\n", i, i) == NULL)
        {
            break;
        }
    }

    if (contents == NULL)
    {
        CHECK(!"out of memory");
        return;
    }

    CHECK_EQ(load_string(&parser, "bulk1.ini", contents), INI_OK);
    free(contents);

    CHECK_EQ(load_string(&parser, "bulk2.ini", "[only]\nk = after\n"), INI_OK);

    for (i = 0; i < 300; i++)
    {
        char name[32];

        snprintf(name, sizeof(name), "bulk%d", i);
        if (ini_parser_get(&parser, name, "k") != NULL)
        {
            ok = 0;
        }
    }

    CHECK_EQ(parser.section_count, 1);
    CHECK_STR(ini_parser_get(&parser, "only", "k"), "after");
    CHECK(parser.sections != NULL);
    if (parser.sections != NULL)
    {
        CHECK(parser.sections->next == NULL);
    }

    g_checks++;
    if (!ok)
    {
        fprintf(stderr, "FAIL %s:%d: reload left stale sections\n", __FILE__, __LINE__);
        g_failures++;
    }

    ini_parser_free(&parser);
}

/* ------------------------------------------------------------------ */
/* keyed hashing (hash flooding protection)                            */
/* ------------------------------------------------------------------ */

/*
 * White-box: read the bucket chain lengths of a section. The layout is an
 * implementation detail, so this exists only to prove that the hash key really
 * participates in bucket assignment; everything else is tested via the public
 * API.
 */
#define PROFILE_MAX_BUCKETS 1024

static size_t bucket_profile(const ini_section_t *section, size_t *counts)
{
    size_t buckets = section->key_bucket_count;
    size_t index;
    const ini_key_t *key;

    if (buckets > PROFILE_MAX_BUCKETS)
    {
        buckets = PROFILE_MAX_BUCKETS;
    }

    for (index = 0; index < buckets; index++)
    {
        counts[index] = 0;
    }

    for (key = section->head; key != NULL; key = key->next)
    {
        for (index = 0; index < buckets; index++)
        {
            const ini_key_t *cursor;
            int found = 0;

            for (cursor = section->key_buckets[index]; cursor != NULL; cursor = cursor->hash_next)
            {
                if (cursor == key)
                {
                    found = 1;
                    break;
                }
            }

            if (found)
            {
                counts[index]++;
                break;
            }
        }
    }

    return buckets;
}

/* Build a one-section config with `count` keys named k0..k(n-1). */
static char *build_key_config(size_t count)
{
    char *contents = NULL;
    size_t length = 0;
    size_t capacity = 0;
    size_t i;

    if (sbuf_append(&contents, &length, &capacity, "[s]\n") == NULL)
    {
        return NULL;
    }

    for (i = 0; i < count; i++)
    {
        if (sbuf_append_fmt(&contents, &length, &capacity,
                            "key%lu = value%lu\n", (unsigned long)i, (unsigned long)i) == NULL)
        {
            return NULL;
        }
    }

    return contents;
}

static int profile_of(ini_parser_t *parser, const char *contents, size_t *counts,
                      size_t *buckets_out)
{
    if (load_string(parser, "profile.ini", contents) != INI_OK)
    {
        return -1;
    }

    if (parser->sections == NULL)
    {
        return -1;
    }

    *buckets_out = bucket_profile(parser->sections, counts);
    return 0;
}

/* Two different keys must spread the same names differently; the same key must
 * reproduce the layout exactly. */
static void test_seed_controls_bucket_assignment(void)
{
    ini_parser_t first;
    ini_parser_t second;
    ini_parser_t repeat;
    char *contents = build_key_config(64);
    size_t profile_first[PROFILE_MAX_BUCKETS];
    size_t profile_second[PROFILE_MAX_BUCKETS];
    size_t profile_repeat[PROFILE_MAX_BUCKETS];
    size_t buckets_first = 0;
    size_t buckets_second = 0;
    size_t buckets_repeat = 0;
    size_t i;
    int differs = 0;
    int reproducible = 1;
    int total = 0;

    if (contents == NULL)
    {
        CHECK(!"out of memory");
        return;
    }

    ini_parser_init(&first);
    ini_parser_init(&second);
    ini_parser_init(&repeat);

    /* An explicit seed survives the load that follows. */
    ini_parser_set_hash_seed(&first, 0x0123456789abcdefULL, 0xfedcba9876543210ULL);
    ini_parser_set_hash_seed(&repeat, 0x0123456789abcdefULL, 0xfedcba9876543210ULL);
    ini_parser_set_hash_seed(&second, 0x1111111111111111ULL, 0x2222222222222222ULL);

    CHECK_EQ(profile_of(&first, contents, profile_first, &buckets_first), 0);
    CHECK_EQ(profile_of(&second, contents, profile_second, &buckets_second), 0);
    CHECK_EQ(profile_of(&repeat, contents, profile_repeat, &buckets_repeat), 0);

    CHECK(buckets_first > 0);
    CHECK_EQ(buckets_first, buckets_second);
    CHECK_EQ(buckets_first, buckets_repeat);

    for (i = 0; i < buckets_first; i++)
    {
        total += (int)profile_first[i];

        if (profile_first[i] != profile_repeat[i])
        {
            reproducible = 0;
        }

        if (profile_first[i] != profile_second[i])
        {
            differs = 1;
        }
    }

    CHECK_EQ(total, 64);
    CHECK(reproducible);    /* same seed -> identical index layout */
    CHECK(differs);         /* different seed -> different spread  */

    /* The index is only an accelerator: every key is still reachable. */
    for (i = 0; i < 64; i++)
    {
        char name[32];
        char expected[32];
        const char *value;

        snprintf(name, sizeof(name), "key%lu", (unsigned long)i);
        snprintf(expected, sizeof(expected), "value%lu", (unsigned long)i);

        value = ini_parser_get(&first, "s", name);
        if (value == NULL || strcmp(value, expected) != 0)
        {
            CHECK(0);
        }
    }
    g_checks++;

    ini_parser_free(&first);
    ini_parser_free(&second);
    ini_parser_free(&repeat);
    free(contents);
}

/* Changing the key on a populated parser must re-index in place. */
static void test_set_hash_seed_reindexes_loaded_parser(void)
{
    ini_parser_t parser;
    char *contents = build_key_config(200);
    size_t i;
    int ok = 1;

    if (contents == NULL)
    {
        CHECK(!"out of memory");
        return;
    }

    ini_parser_init(&parser);
    CHECK_EQ(load_string(&parser, "reindex.ini", contents), INI_OK);

    /* First re-index an already populated parser. */
    ini_parser_set_hash_seed(&parser, 0xdeadbeefcafebabeULL, 0x00c0ffee12345678ULL);

    for (i = 0; i < 200; i++)
    {
        char name[32];

        snprintf(name, sizeof(name), "key%lu", (unsigned long)i);
        if (ini_parser_get(&parser, "s", name) == NULL)
        {
            ok = 0;
        }
    }

    g_checks++;
    if (!ok)
    {
        fprintf(stderr, "FAIL %s:%d: lookups broken after set_hash_seed\n", __FILE__, __LINE__);
        g_failures++;
    }

    /* Order and section structure are untouched by re-indexing. */
    CHECK_EQ(parser.section_count, 1);
    CHECK(parser.sections != NULL);
    if (parser.sections != NULL && parser.sections->head != NULL)
    {
        CHECK_STR(parser.sections->head->name, "key0");
        CHECK_STR(parser.sections->head->value, "value0");
    }

    /* Re-seeding again must be equally safe. */
    ini_parser_set_hash_seed(&parser, 1, 2);
    CHECK(ini_parser_get(&parser, "s", "key1") != NULL);
    if (ini_parser_get(&parser, "s", "key199") == NULL)
    {
        CHECK(0);
    }
    g_checks++;

    /* And seeding an empty parser is a no-op on the empty index. */
    {
        ini_parser_t empty;

        ini_parser_init(&empty);
        ini_parser_set_hash_seed(&empty, 7, 8);
        CHECK_EQ(empty.hash_seeded, 1);
        CHECK(ini_parser_get(&empty, "s", "key1") == NULL);
        ini_parser_free(&empty);
    }

    ini_parser_free(&parser);
    free(contents);
}

static int os_entropy_available(void)
{
#if defined(_WIN32)
    return 0;
#else
    FILE *file = fopen("/dev/urandom", "rb");

    if (file == NULL)
    {
        return 0;
    }

    fclose(file);
    return 1;
#endif
}

/* The default key is drawn lazily on the first load, never zero, and unique per
 * parser whenever the OS entropy source is usable. */
static void test_default_hash_key(void)
{
    ini_parser_t first;
    ini_parser_t second;
    ini_parser_t zeroed = {0};

    ini_parser_init(&first);
    ini_parser_init(&second);

    CHECK_EQ(first.hash_seeded, 0); /* nothing drawn before the first load */

    CHECK_EQ(load_string(&first, "seed_one.ini", "[s]\nk = v\n"), INI_OK);
    CHECK_EQ(load_string(&second, "seed_two.ini", "[s]\nk = v\n"), INI_OK);

    CHECK_EQ(first.hash_seeded, 1);
    CHECK_EQ(second.hash_seeded, 1);
    CHECK(!(first.hash_key[0] == 0 && first.hash_key[1] == 0));

    if (os_entropy_available())
    {
        CHECK(first.hash_key[0] != second.hash_key[0] ||
              first.hash_key[1] != second.hash_key[1]);
    }

    /* A zero initialised parser (documented as supported) is seeded on load. */
    CHECK_EQ(load_string(&zeroed, "seed_zero.ini", "[s]\nk = v\n"), INI_OK);
    CHECK_EQ(zeroed.hash_seeded, 1);
    CHECK_STR(ini_parser_get(&zeroed, "s", "k"), "v");

    ini_parser_free(&first);
    ini_parser_free(&second);
    ini_parser_free(&zeroed);
}

/* ------------------------------------------------------------------ */
/* lifetime                                                            */
/* ------------------------------------------------------------------ */

static void test_reload_replaces_content(void)
{
    ini_parser_t parser;
    char *path = write_file("reload.ini", "[first]\na = 1\n");

    ini_parser_init(&parser);

    CHECK(path != NULL);
    if (path == NULL)
    {
        return;
    }

    CHECK_EQ(ini_parser_load(&parser, path), INI_OK);
    CHECK_STR(ini_parser_get(&parser, "first", "a"), "1");

    /* Overwrite the file and load again: the old contents must be gone. */
    {
        FILE *file = fopen(path, "wb");

        CHECK(file != NULL);
        if (file != NULL)
        {
            fputs("[second]\nb = 2\n", file);
            fclose(file);
        }
    }

    CHECK_EQ(ini_parser_load(&parser, path), INI_OK);
    CHECK_STR(ini_parser_get(&parser, "second", "b"), "2");
    CHECK(ini_parser_get(&parser, "first", "a") == NULL);

    ini_parser_free(&parser);
    remove(path);
    free(path);
}

static void test_free_is_idempotent(void)
{
    ini_parser_t parser;

    ini_parser_init(&parser);

    CHECK_EQ(load_string(&parser, "free.ini", "[s]\nk = v\n"), INI_OK);
    CHECK_STR(ini_parser_get(&parser, "s", "k"), "v");

    ini_parser_free(&parser);
    CHECK(parser.sections == NULL);

    /* Values are owned by the parser: the lookup must report absence now. */
    CHECK(ini_parser_get(&parser, "s", "k") == NULL);

    ini_parser_free(&parser); /* second free must be harmless */
    CHECK(parser.sections == NULL);

    /* A freed parser can be reused, including after init. */
    ini_parser_init(&parser);
    CHECK_EQ(load_string(&parser, "reuse.ini", "[s]\nk = v2\n"), INI_OK);
    CHECK_STR(ini_parser_get(&parser, "s", "k"), "v2");

    ini_parser_free(&parser);
}

/* ------------------------------------------------------------------ */

int main(int argc, char **argv)
{
    const char *env = getenv("INI_TEST_TMPDIR");

    if (argc > 1)
    {
        g_tmpdir = argv[1];
    }
    else if (env != NULL && *env != '\0')
    {
        g_tmpdir = env;
    }

    printf("scratch directory: %s\n", g_tmpdir);

    test_basic();
    test_whitespace_and_tabs();
    test_empty_values();
    test_equals_in_value();
    test_comments();
    test_quoted_values();
    test_duplicate_key_last_wins();
    test_duplicate_section_merges();
    test_default_section();
    test_default_section_only();
    test_crlf_and_no_final_newline();
    test_long_lines_are_supported();
    test_many_long_lines();
    test_long_line_with_comment();
    test_missing_file();
    test_syntax_errors();
    test_error_state_reset_on_success();
    test_null_arguments();
    test_strerror();
    test_get_copy();
    test_seed_controls_bucket_assignment();
    test_set_hash_seed_reindexes_loaded_parser();
    test_default_hash_key();
    test_large_section_table();
    test_large_key_table();
    test_mixed_large_table();
    test_duplicate_key_after_growth();
    test_reload_resets_index();
    test_reload_replaces_content();
    test_free_is_idempotent();

    printf("\n%d checks, %d failure(s)\n", g_checks, g_failures);
    return (g_failures == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
