# D01: POSIX C Source for analyze-service

## Context
F04 needs `fork`/`exec`/`pipe`/`fdopen`/`kill` which are POSIX extensions, not plain ISO C11.

## Decision
Compile with `-D_POSIX_C_SOURCE=200809L` alongside `-std=c11` instead of switching to `-std=gnu11`. This makes POSIX dependency explicit and portable.

## Consequence
Code compiles cleanly with `-Wall -Wextra -O2` and zero warnings on GCC.
