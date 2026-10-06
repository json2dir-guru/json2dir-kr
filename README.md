# json2dir-kr

json2dir in K&R C: pre-ANSI 1978 style (old-style definitions, no prototypes, no `void`/`const`/`enum`), hand-written JSON parser and UTF-8 check.

    gcc -std=gnu89 -fno-builtin -O2 -o out/json2dir json2dir.c && out/json2dir < document.json
