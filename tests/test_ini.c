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
    test_reload_replaces_content();
    test_free_is_idempotent();

    printf("\n%d checks, %d failure(s)\n", g_checks, g_failures);
    return (g_failures == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
