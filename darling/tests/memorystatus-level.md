# Memory status level

Run the controlled-input host regression with Clang and its sanitizers:

```sh
python3 darling/tests/memorystatus-level.py
```

It compiles the actual meminfo parser, reader, percentage helper and handler,
substituting a temporary file only for procfs I/O. It checks 0%, truncation to
50%, 100%, out-of-range clamping, missing/malformed fields, unavailable procfs,
overflow rejection, descriptor cleanup, sizing, short buffers and denied writes.

Compile `memorystatus-level.c` as a Darwin executable with the configured Darling
SDK and libSystem. Run it in the matching private runtime:

```sh
darling shell /Volumes/SystemRoot/absolute/path/to/memorystatus-level
```

The target test exercises named and numeric lookup through the actual sysctl
dispatcher, size queries, range and read-only behavior. The old kernel library
fails the first query with `ENOENT`. The current value is dynamic; the host test
provides the controlled arithmetic cases.
