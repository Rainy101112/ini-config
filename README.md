# ini-config

A simple Initialization File(INI configuration) parser in C (C99, no
dependencies, MIT licensed).

```ini
; example.ini
app_name = ini-config demo

[server]
host = 127.0.0.1
port = 8080   ; inline comments need whitespace before the marker
greeting = "Hello, world ; quoting keeps the marker"
```

## Build

```sh
make            # build/libini.a
make examples   # build/example
make test       # build and run the test suite
make clean
```

Options: `make CC=clang`, `make CFLAGS="-O1 -g -fsanitize=address"`.

## Use

```c
#include "ini.h"

ini_parser_t parser;
ini_parser_init(&parser);

int status = ini_parser_load(&parser, "example.ini");
if (status != INI_OK)
{
    fprintf(stderr, "%s: %s (line %d)\n", "example.ini", ini_strerror(status),
            parser.last_error_line);
    ini_parser_free(&parser);
    return 1;
}

const char *host = ini_parser_get(&parser, "server", "host");
if (host != NULL)
{
    printf("host = %s\n", host);
}

ini_parser_free(&parser);
```

The library is a single translation unit, so you can also just compile `ini.c`
alongside your own sources. Link with `build/libini.a` or drop `ini.c` into your
project.

## API

```c
typedef enum ini_status {
    INI_OK              =  0,
    INI_ERROR           = -1,  /* unspecified failure (legacy catch-all) */
    INI_ERROR_ARGUMENT  = -2,  /* NULL pointer or zero buffer size        */
    INI_ERROR_OPEN      = -3,  /* cannot open the configuration file      */
    INI_ERROR_READ      = -4,  /* read error while parsing                */
    INI_ERROR_SYNTAX    = -5,  /* malformed line; see last_error_line     */
    INI_ERROR_MEMORY    = -6,  /* allocation failure                      */
    INI_ERROR_NOT_FOUND = -7,  /* section or key does not exist           */
    INI_ERROR_TOO_SMALL = -8   /* destination buffer too small            */
} ini_status_t;
```

| Function | Description |
| --- | --- |
| `void ini_parser_init(ini_parser_t *)` | Zero a parser. `ini_parser_load` also works on a zero initialised struct, but call this on one that was used before. |
| `int ini_parser_load(ini_parser_t *, const char *path)` | Parse a file into the parser, replacing any previous contents. Returns `INI_OK` or a negative `ini_status_t`. |
| `const char *ini_parser_get(const ini_parser_t *, const char *section, const char *key)` | Return the value, or `NULL` when the section or key is absent. |
| `int ini_parser_get_copy(const ini_parser_t *, const char *section, const char *key, char *out, size_t out_size)` | Copy the value into a caller-owned buffer (always NUL terminated). Returns `INI_OK`, `INI_ERROR_NOT_FOUND`, `INI_ERROR_TOO_SMALL` or `INI_ERROR_ARGUMENT`. |
| `void ini_parser_free(ini_parser_t *)` | Release everything and reset `parser->sections` to `NULL`. Safe to call twice. |
| `const char *ini_strerror(int status)` | Human readable text for a status code; never returns `NULL`. |

`ini_parser_t` keeps the last failure for diagnostics:

```c
struct ini_parser {
    ini_section_t *sections;
    int last_error;       /* INI_OK until a load fails */
    int last_error_line;  /* 1-based offending line    */
};
```

`ini_parser_get` returns a pointer into the parser, invalidated by
`ini_parser_free` or the next `ini_parser_load`. Use `ini_parser_get_copy` when
you need the value to outlive the parser.

The structs are public (`ini_section`, `ini_key` are singly linked lists), so
you can iterate over the whole file without any extra API — see
`examples/example.c`.

## Syntax

* `[section]` starts a section; surrounding whitespace is trimmed and repeated
  headers merge into the existing section.
* Keys that appear **before** the first header go into the default section,
  whose name is the empty string (`INI_DEFAULT_SECTION`):
  `ini_parser_get(&parser, "", "app_name")`.
* `key = value` assigns; only the first `=` splits, so values may contain `=`.
* Both sides are trimmed of whitespace.
* A value wrapped in `"` or `'` is unquoted and kept verbatim, including inner
  whitespace and comment markers: `greeting = "a ; b"` yields `a ; b`.
  Quotes only open at the start of a token, so `it's` stays literal. There are
  no escape sequences and quotes do not nest.
* `;` and `#` start a comment at the beginning of a line, anywhere they are
  preceded by whitespace outside quotes, or after a section header.
* Blank lines and `\r\n` line endings are handled. A repeated key keeps the last
  value.
* Lines may be arbitrarily long: the line buffer grows as needed.

## Known limitations

* The parser is strict: a line that is neither a header, a comment, nor
  `key = value` fails the whole load with `INI_ERROR_SYNTAX`. So does a key
  without a name (`= value`).
* Non-whitespace text after `]` in a section header is silently ignored.
* There is no escape mechanism, so a value cannot contain the same quote
  character that delimits it, and a quoted value cannot span lines.
* Section and key lookup is a linear scan over the linked lists, so lookups are
  O(n) in the number of sections/keys.
* NUL bytes inside the file are not supported; a line is treated as a C string.
* `ini_parser_load` keeps the first occurrence order of a key when a later
  duplicate overrides its value.

The test suite in `tests/test_ini.c` documents the behaviour above, including
the edge cases of comment stripping and quoting.

## License

MIT. See `LICENSE`.
