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

These targets are diagnostics only, they are not part of `make`: 

```sh
make bench       # micro benchmark, 20000 keys by default
make bench N=100000
make bench-HEAD  # same benchmark against the last commit's ini.c
make flood-demo  # collision flood: known key vs. random key
```

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
| `void ini_parser_set_hash_seed(ini_parser_t *, uint64_t seed0, uint64_t seed1)` | Override the per-parser hash key. Optional; exists for reproducible behaviour and for callers with their own CSPRNG. |

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
`examples/example.c`. Fields marked "internal" in `ini.h` back the hash index:
read them if you like, but do not modify them or rely on their layout.

## Lookup performance

Section and key lookups go through a hash index, so `ini_parser_get` is O(1) on
average instead of O(n). Both tables use separate chaining, start at 8 buckets
and double once the load factor would exceed 3/4. Appending a section or key is
O(1) thanks to the `tail` pointers, so parsing is O(n) in the file size.

Names are hashed with **SipHash-2-4 under a per-parser 128 bit secret**. The
secret is drawn the first time a file is loaded: 16 bytes from `/dev/urandom`
on Unix, mixed with ASLR/clock values so that even a failed entropy read is not
predictable across runs. See [Hash flooding](#hash-flooding) for why that
matters and what the limits are.

The index is purely an accelerator: file order is still carried by the
`sections` / `head` linked lists, so iteration output is unchanged and a
repeated key keeps its original position when its value is overridden.

`make bench` shows the difference. One representative run of 20000 keys with
`-O2` on one x86-64 core, comparing against `make bench-HEAD`:

| 20k keys | linked list (`make bench-HEAD`) | hash index (`make bench`) |
| --- | --- | --- |
| parse | 2755 ms | 13.7 ms |
| 20k sequential `get` | 1367 ms | 3.1 ms |
| 20k random `get` | 1337 ms | 4.5 ms |
| 20k absent `get` | 3693 ms | 1.9 ms |

Hashing a name with keyed SipHash-2-4 costs tens of nanoseconds, which is why the
`get` rows are slower than an unkeyed hash would be. That difference is the
price of the flooding resistance described below; the tables here are still two
to three orders of magnitude ahead of the linked list version.

Per-`get` cost stays roughly flat as the key count grows (the mild rise is
cache pressure, not algorithm), while the linked list version grows
quadratically:

| keys | 2000 | 5000 | 10000 | 20000 | 50000 | 200000 |
| --- | --- | --- | --- | --- | --- | --- |
| hash, ns/random `get` | 154 | 143 | 169 | 191 | 346 | 430 |
| list, ns/random `get` | 5166 | 15889 | 32394 | — | — | — |

Both builds report the same checksum, which is a cheap cross-check that the
index returns exactly the values the linear scan did.

## Hash flooding

A hash table keyed by attacker-controlled strings is a denial of service vector:
with a fixed, publicly known hash function, an attacker can precompute thousands
of key names that all map to the same bucket, turning every `get` into a linear
scan and a parse into O(n²) work. This library therefore hashes with
SipHash-2-4 under a secret key that is generated per parser, so the colliding
names cannot be computed offline. This is the same approach used by Rust's,
Python's and Ruby's hash maps.

The secret is 128 bits and is drawn once per parser, on the first
`ini_parser_load`:

* on Unix, 16 bytes from `/dev/urandom` (read with plain stdio, so no feature
  test macros or platform headers are involved);
* always mixed with the parser's address (ASLR), a fresh heap address, the wall
  clock and `clock()`, so a failed entropy read still yields a key that is not
  predictable across runs;
* an all-zero key is never used.

If you want to be independent of that source — for example on a platform without
`/dev/urandom`, or in a library that already runs inside a hardened process —
pass your own CSPRNG output:

```c
ini_parser_set_hash_seed(&parser, seed0, seed1);
```

Passing a fixed pair also makes behaviour fully reproducible, which is handy in
tests and reproducible builds. The call is safe at any time: an already parsed
file is re-indexed in place (no allocation, file order untouched).

`make flood-demo` reproduces the whole thing, in both directions:

```
  key known to attacker  buckets= 8192 longest chain= 662  load=  22.20 ms  get=   2665 ns
  default random key     buckets= 8192 longest chain=   6  load=   2.36 ms  get=    113 ns
```

The demo brute forces 5000 key names that collide for a *known* key, writes them
to a file, and parses that same file twice. With the secret known the chains
collapse into 662 entries and lookups get ~24x slower; with the default random
key the identical file spreads over the table and stays fast. (The demo also
asserts both directions, so it fails if the protection regresses.)

What this does **not** protect against: an attacker who can read the process
memory and therefore the key. At that point they can craft collisions again, but
they also already have far more direct capabilities.

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
* Flooding protection depends on unpredictable secrets. Where `/dev/urandom`
  cannot be read (notably Windows, which currently uses the portable fallback
  mix), the default key is weaker; call `ini_parser_set_hash_seed()` with
  CSPRNG output there if you parse untrusted input.
* NUL bytes inside the file are not supported; a line is treated as a C string.
* `ini_parser_load` keeps the first occurrence order of a key when a later
  duplicate overrides its value.

`make test` runs two binaries: `tests/test_ini.c` (behaviour, including the edge
cases of comment stripping and quoting) and `tests/test_siphash.c`, which checks
the internal hasher against the 64 reference vectors published with SipHash.

## License

MIT. See `LICENSE`.
