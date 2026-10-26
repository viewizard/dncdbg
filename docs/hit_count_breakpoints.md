## Hit count breakpoints

The 'hit count condition' controls how many times a breakpoint needs to be hit before it will 'break' execution.

The hit count syntax supported by the DNCDbg debugger, identical to VsDbg, is either an integer or one of the operators <, <=, ==, >, >=, % followed by an integer.


| Syntax | Stop when the count is |
| --- | --- |
| `<N` | Less than N |
| `<=N` | Less than or equal to N |
| `==N` | Equal to N |
| `>N` | Greater than N |
| `N` or `>=N` | Greater than or equal to N |
| `%N` | An exact multiple of N |

Some examples:
* `>5` break always after 5 hits.
* `<3` break on the first two hits only.
* `7` same as `>=7`.
* `%2` break on every other hit.
