/*
 *  filename: tests/test_ini.c
 *  description: Self-contained test suite for the ini-config parser.
 *  author: Rainy101112(https://github.com/Rainy101112)
 *  license: MIT license
 *
 *  No external test framework: the harness below is ~40 lines so that the
 *  project keeps zero dependencies. Run with:
 *
 *      make test
 *      ./build/test_ini            # uses ./ as scratch dir
 *      INI_TEST_TMPDIR=/tmp ./build/test_ini
 *
 *  The suite pins down *current* behaviour. Cases marked "known limitation"
 *  assert the quirks documented in the README rather than ideal behaviour;
 *  update them when the parser is fixed.
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

/* Load a config from an inline string. Returns 0 on success, -1 otherwise. */
static int load_string(ini_parser_t *parser, const char *name, const char *contents)
{
    char *path = write_file(name, contents);
    int result;

    if (path == NULL)
    {
        return -1;
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
    ini_parser_t parser = {0};

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
    ini_parser_t parser = {0};

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

static void test_empty_and_quoted_values(void)
{
    ini_parser_t parser = {0};

    CHECK_EQ(load_string(&parser,
                         "empty.ini",
                         "[s]\n"
                         "empty =\n"
                         "spaces =    \n"
                         "quoted = \"  keep me  \"\n"),
             0);

    CHECK_STR(ini_parser_get(&parser, "s", "empty"), "");
    CHECK_STR(ini_parser_get(&parser, "s", "spaces"), "");
    /* Quotes are not special: only outer whitespace is trimmed. */
    CHECK_STR(ini_parser_get(&parser, "s", "quoted"), "\"  keep me  \"");

    ini_parser_free(&parser);
}

static void test_equals_in_value(void)
{
    ini_parser_t parser = {0};

    CHECK_EQ(load_string(&parser, "equals.ini", "[s]\nurl = http://x/?a=1&b=2\n"), 0);

    /* Only the first '=' splits; the rest belongs to the value. */
    CHECK_STR(ini_parser_get(&parser, "s", "url"), "http://x/?a=1&b=2");

    ini_parser_free(&parser);
}

/* ------------------------------------------------------------------ */
/* comments                                                            */
/* ------------------------------------------------------------------ */

static void test_comments(void)
{
    ini_parser_t parser = {0};

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
                         "g = 2\n"),
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

    ini_parser_free(&parser);
}

/* ------------------------------------------------------------------ */
/* overriding and merging                                              */
/* ------------------------------------------------------------------ */

static void test_duplicate_key_last_wins(void)
{
    ini_parser_t parser = {0};

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
    ini_parser_t parser = {0};
    const ini_section_t *section;
    int count = 0;

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

/* ------------------------------------------------------------------ */
/* line endings                                                        */
/* ------------------------------------------------------------------ */

static void test_crlf_and_no_final_newline(void)
{
    ini_parser_t parser = {0};

    CHECK_EQ(load_string(&parser,
                         "crlf.ini",
                         "[s]\r\n"
                         "a = 1\r\n"
                         "b = 2"),
             0);

    /* trim() drops the '\r' as trailing whitespace. */
    CHECK_STR(ini_parser_get(&parser, "s", "a"), "1");
    CHECK_STR(ini_parser_get(&parser, "s", "b"), "2");

    ini_parser_free(&parser);
}

/* ------------------------------------------------------------------ */
/* error handling                                                      */
/* ------------------------------------------------------------------ */

static void test_missing_file(void)
{
    ini_parser_t parser = {0};

    CHECK_EQ(ini_parser_load(&parser, "this/file/does/not/exist.ini"), -1);
    CHECK(parser.sections == NULL);
    CHECK(ini_parser_get(&parser, "s", "k") == NULL);

    ini_parser_free(&parser);
}

static void test_syntax_errors(void)
{
    static const char *const cases[] = {
        "[unclosed\nkey = 1\n",          /* missing ']'                  */
        "[]\nkey = 1\n",                 /* empty section name           */
        "key = 1\n",                     /* key before any section       */
        "[s]\nnot a key value pair\n",   /* line without '='            */
        "[s]\n = no key\n",              /* empty key                    */
    };
    size_t i;
    char name[32];

    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
    {
        ini_parser_t parser = {0};
        int result;

        snprintf(name, sizeof(name), "bad_%lu.ini", (unsigned long)i);
        result = load_string(&parser, name, cases[i]);

        if (result != -1)
        {
            fprintf(stderr, "FAIL: case %lu should have failed\n", (unsigned long)i);
            fprintf(stderr, "  input: %s", cases[i]);
            g_failures++;
        }
        g_checks++;

        /* A failed load leaves the parser empty and still frees cleanly. */
        CHECK(parser.sections == NULL);
        ini_parser_free(&parser);
    }
}

static void test_null_arguments(void)
{
    ini_parser_t parser = {0};

    CHECK_EQ(ini_parser_load(NULL, "x.ini"), -1);
    CHECK_EQ(ini_parser_load(&parser, NULL), -1);

    CHECK(ini_parser_get(NULL, "s", "k") == NULL);
    CHECK(ini_parser_get(&parser, NULL, "k") == NULL);
    CHECK(ini_parser_get(&parser, "s", NULL) == NULL);

    ini_parser_free(NULL); /* must not crash */

    ini_parser_free(&parser);
}

static void test_reload_replaces_content(void)
{
    ini_parser_t parser = {0};
    char *path = write_file("reload.ini", "[first]\na = 1\n");

    CHECK(path != NULL);
    if (path == NULL)
    {
        return;
    }

    CHECK_EQ(ini_parser_load(&parser, path), 0);
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

    CHECK_EQ(ini_parser_load(&parser, path), 0);
    CHECK_STR(ini_parser_get(&parser, "second", "b"), "2");
    CHECK(ini_parser_get(&parser, "first", "a") == NULL);

    ini_parser_free(&parser);
    remove(path);
    free(path);
}

static void test_free_is_idempotent(void)
{
    ini_parser_t parser = {0};

    CHECK_EQ(load_string(&parser, "free.ini", "[s]\nk = v\n"), 0);
    CHECK_STR(ini_parser_get(&parser, "s", "k"), "v");

    ini_parser_free(&parser);
    CHECK(parser.sections == NULL);

    /* Values are owned by the parser: the lookup must report absence now. */
    CHECK(ini_parser_get(&parser, "s", "k") == NULL);

    ini_parser_free(&parser); /* second free must be harmless */
    CHECK(parser.sections == NULL);
}

/* ------------------------------------------------------------------ */
/* known limitations                                                   */
/* ------------------------------------------------------------------ */

/*
 * Lines longer than the parser's internal 1024-byte fgets() buffer are split
 * in two. The tail usually has no '=', so the load fails with -1 instead of
 * silently truncating the value. Assert that, so that fixing the buffer size
 * (or switching to getline) makes this test fail loudly and get updated.
 */
static void test_long_line_fails_the_load(void)
{
    ini_parser_t parser = {0};
    char *contents;
    size_t i;
    size_t length = 2000;

    contents = (char *)malloc(length + 16);
    CHECK(contents != NULL);
    if (contents == NULL)
    {
        return;
    }

    memcpy(contents, "[s]\nkey = ", 10);
    for (i = 0; i < length; i++)
    {
        contents[10 + i] = 'a';
    }
    contents[10 + length] = '\n';
    contents[11 + length] = '\0';

    CHECK_EQ(load_string(&parser, "longline.ini", contents), -1);
    CHECK(parser.sections == NULL);

    ini_parser_free(&parser);
    free(contents);
}

/*
 * A comment marker that follows whitespace inside a quoted value still starts
 * a comment, because the parser has no concept of quotes.
 */
static void test_quotes_do_not_protect_comment_markers(void)
{
    ini_parser_t parser = {0};

    CHECK_EQ(load_string(&parser, "quote_comment.ini", "[s]\nmsg = \"a # b\"\n"), 0);

    CHECK_STR(ini_parser_get(&parser, "s", "msg"), "\"a");

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
    test_empty_and_quoted_values();
    test_equals_in_value();
    test_comments();
    test_duplicate_key_last_wins();
    test_duplicate_section_merges();
    test_crlf_and_no_final_newline();
    test_missing_file();
    test_syntax_errors();
    test_null_arguments();
    test_reload_replaces_content();
    test_free_is_idempotent();
    test_long_line_fails_the_load();
    test_quotes_do_not_protect_comment_markers();

    printf("\n%d checks, %d failure(s)\n", g_checks, g_failures);
    return (g_failures == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
