# ini-config

A simple Initialization File(INI configuration) parser in C (C99, no
dependencies, MIT licensed).

```ini
; example.ini
[server]
host = 127.0.0.1
port = 8080   ; inline comments need whitespace before the marker
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

ini_parser_t parser = {0};   /* must be zero initialised */

if (ini_parser_load(&parser, "example.ini") != 0)
{
    return 1;                /* -1: cannot open or syntax error */
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

| Function | Description |
| --- | --- |
| `int ini_parser_load(ini_parser_t *, const char *path)` | Parse a file into the parser, replacing any previous contents. Returns `0` on success, `-1` on error. |
| `const char *ini_parser_get(const ini_parser_t *, const char *section, const char *key)` | Return the value, or `NULL` when the section or key is absent. |
| `void ini_parser_free(ini_parser_t *)` | Release everything and reset `parser->sections` to `NULL`. Safe to call twice. |

`ini_parser_load` calls `ini_parser_free` first, so the parser **must be zero
initialised** (`ini_parser_t parser = {0};`) or it will read an uninitialised
pointer.

The structs are public (`ini_section`, `ini_key` are singly linked lists), so
you can iterate over the whole file without any extra API — see
`examples/example.c`.

## Syntax

* `[section]` starts a section; surrounding whitespace is trimmed and repeated
  headers merge into the existing section.
* `key = value` assigns; only the first `=` splits, so values may contain `=`.
* Both sides are trimmed of whitespace; quotes are not special.
* `;` and `#` start a comment at the beginning of a line, or anywhere they are
  preceded by whitespace.
* Blank lines and `\r\n` line endings are handled. A repeated key keeps the last
  value.

## Known limitations

* Lines longer than 1023 bytes are split by the internal `fgets` buffer and
  usually cause the load to fail with `-1` instead of being supported.
* Keys that appear before the first `[section]` header make the load fail;
  there is no default/implicit section.
* No quotes support: `msg = "a # b"` yields `"a`.
* All errors are reported as `-1`; there is no way to tell "file not found"
  from "syntax error", and no line number is reported.
* `ini_parser_get` returns a pointer into the parser, so it dangles after
  `ini_parser_free` or a subsequent `ini_parser_load`.

The test suite in `tests/test_ini.c` documents the current behaviour,
including the limitations above.

## License

MIT. See `LICENSE`.
