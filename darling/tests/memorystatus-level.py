#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Host regression using the real meminfo reader, parser, and sysctl handler.

Usage: python3 darling/tests/memorystatus-level.py
Only procfs I/O is substituted with a temporary file. Requires Clang sanitizers.
"""
import pathlib
import re
import subprocess
import tempfile

root = pathlib.Path(__file__).resolve().parents[1] / "src/libsystem_kernel/emulation"
vm = (root / "src/xnu_syscall/bsd/helper/misc/sysctl_vm.c").read_text()
kern = (root / "src/xnu_syscall/bsd/helper/misc/sysctl_kern.c").read_text()
header = (root / "include/xnu_syscall/bsd/impl/misc/sysctl.h").read_text()
simple = (root / "src/common/simple.c").read_text()
reader = (root / "src/xnu_syscall/bsd/helper/misc/readline.c").read_text()

def extract(pattern, source):
    match = re.search(pattern, source, re.M | re.S)
    if not match:
        raise SystemExit("required implementation missing: " + pattern)
    return match.group()

registration = r'\.name = "memorystatus_level", \.handler = handle_memorystatus_level'
if not re.search(registration, kern):
    raise SystemExit("kern.memorystatus_level is not registered")
parts = [
    extract(r"struct meminfo \{.*?^\};", vm),
    extract(r"#define sysctl_handle_size\(size\).*?(?=^typedef)", header),
    extract(r"unsigned long long __simple_atoi\(.*?^\}", simple),
    extract(r"const char\* _readline\(.*?^\}", reader),
    extract(r"void _readline_init\(.*?^\}", reader),
    extract(r"static bool parse_meminfo_line\(.*?^\};", vm),
    extract(r"static int read_meminfo\(.*?^\};", vm),
    extract(r"int _sysctl_memorystatus_level\(.*?^\}", vm),
    extract(r"static sysctl_handler\(handle_memorystatus_level\) \{.*?^\}", kern),
]
program = r'''
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#define BSD_O_RDONLY O_RDONLY
#define sysctl_handler(_hname) long _hname(const int* name, int nlen, void* old, unsigned long* oldlen, void* _new, unsigned long newlen)
struct rdline_buffer { char buffer[1024]; unsigned int usage, next; };
static FILE *fixture;
static int open_error, opened, closed;
static int sys_open_nocancel(const char *path, int flags, int mode) {
    assert(strcmp(path, "/proc/meminfo") == 0 && flags == O_RDONLY);
    if (open_error) return -open_error;
    rewind(fixture);
    ++opened;
    return dup(fileno(fixture));
}
static int close_internal(int fd) { ++closed; return close(fd); }
static int sys_read_nocancel(int fd, void *out, size_t size) { return read(fd, out, size); }
void _readline_init(struct rdline_buffer *buf);
IMPLEMENTATION
static void check(const char *text, long expected, int value) {
    fixture = tmpfile(); assert(fixture);
    assert(fputs(text, fixture) >= 0 && fflush(fixture) == 0);
    unsigned long size = sizeof(int);
    int out = -1;
    long result = handle_memorystatus_level(NULL, 0, &out, &size, NULL, 0);
    assert(result == expected && size == sizeof(int));
    assert(out == (expected == 0 ? value : -1));
    assert(opened == closed);
    fclose(fixture);
}
int main(void) {
    unsigned long size = 0;
    assert(handle_memorystatus_level(NULL, 0, NULL, &size, NULL, 0) == 0);
    assert(size == sizeof(int) && opened == 0);
    int out = -1;
    size = 1;
    assert(handle_memorystatus_level(NULL, 0, &out, &size, NULL, 0) == -EINVAL);
    assert(out == -1 && opened == 0);
    size = sizeof(int);
    assert(handle_memorystatus_level(NULL, 0, &out, &size, &out, sizeof(out)) == -EPERM);
    assert(handle_memorystatus_level(NULL, 0, &out, NULL, NULL, 0) == -EINVAL);
    check("MemTotal: 1000 kB\nMemAvailable: 0 kB\n", 0, 0);
    check("MemTotal: 1000 kB\nMemAvailable: 507 kB\n", 0, 50);
    check("MemAvailable:\t1000 kB\nMemTotal:\t1000 kB\n", 0, 100);
    check("MemTotal: 1000 kB\nMemAvailable: 1001 kB\n", 0, 100);
    check("MemTotal: 0 kB\nMemAvailable: 0 kB\n", -ENOENT, -1);
    check("MemTotal: 1000 kB\n", -ENOENT, -1);
    check("MemAvailable: 500 kB\n", -ENOENT, -1);
    check("MemTotal: 1000 kB\nMemAvailable: nope kB\n", -ENOENT, -1);
    check("MemTotal: 1000 kB\nMemAvailable: 500 MB\n", -ENOENT, -1);
    check("MemTotal: 18446744073709551615 kB\nMemAvailable: 18446744073709551614 kB\n", -EOVERFLOW, -1);
    fixture = tmpfile(); assert(fixture);
    assert(fputs("MemTotal: 1000 kB\nMemFree: 100 kB\nMemAvailable: 500 kB\nBuffers: 10 kB\nCached: 20 kB\nSReclaimable: 30 kB\nShmem: 40 kB\nSwapTotal: 200 kB\nSwapFree: 150 kB\n", fixture) >= 0);
    assert(fflush(fixture) == 0);
    struct meminfo parsed;
    assert(read_meminfo(&parsed) == 0);
    assert(parsed.total == 1000 && parsed.free == 100 && parsed.available == 500 && parsed.has_available);
    assert(parsed.buffers == 10 && parsed.cached == 20 && parsed.slab_reclaimable == 30 && parsed.shmem == 40);
    assert(parsed.swap_total == 200 && parsed.swap_free == 150);
    fclose(fixture);
    open_error = EACCES;
    assert(_sysctl_memorystatus_level(&out) == -EACCES);
    assert(opened == closed);
    puts("PASS: real procfs parser, percentage, failure, buffer, and descriptor checks");
}
'''.replace("IMPLEMENTATION", "\n".join(parts))
with tempfile.TemporaryDirectory(prefix="memorystatus-level-") as directory:
    source = pathlib.Path(directory) / "probe.c"
    output = pathlib.Path(directory) / "probe"
    source.write_text(program)
    subprocess.run(["clang", "-O1", "-g", "-Wall", "-fsanitize=address,undefined", str(source), "-o", str(output)], check=True)
    subprocess.run([str(output)], check=True)
