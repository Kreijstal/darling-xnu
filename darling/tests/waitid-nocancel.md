# waitid_nocancel

Compile `waitid-nocancel.c` as a Darwin Mach-O against the configured SDK and
libSystem, then run `darling shell /path/to/waitid-nocancel` in a private prefix
with the rebuilt kernel library. The baseline syscall416 returns ENOSYS.
The fixed test checks WNOHANG with no ready child, an actual child exiting42,
WNOWAIT leaving it waitable, ordinary waitpid reaping and subsequent ECHILD.
It deliberately calls the numeric noncancelable syscall rather than waitid.

The implementation reuses the existing conversion path. Signal-termination
status conversion and cancellation races are outside this regression's coverage.
Provenance: published XNU `bsd/kern/syscalls.master` entries173/416, the vendored
wait declarations, and authored API tests (rungs1,2,4). No guest internals used.

Additional integration check: `dotnet build darling/tests/waitid-managed -o out`,
then execute ProcessProbe.dll with an official macOS .NET10 runtime inside the
prefix. It performs three Process.Start/WaitForExit cycles and requires exit42.
This passed on ARM64 Asahi using .NET10.0.13 and the other previously tested
runtime fixes; it is not a claim that arbitrary managed applications work.
