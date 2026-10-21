#include <darling/emulation/xnu_syscall/bsd/impl/wrapped/sem_open.h>
#include <sys/errno.h>
#include <string.h>
#include <limits.h>

#include <darling/emulation/common/base.h>
#include <darling/emulation/conversion/errno.h>
#include <darling/emulation/conversion/fcntl/open.h>
#include <darling/emulation/xnu_syscall/bsd/impl/fcntl/open.h>
#include <darling/emulation/linux_premigration/linux-syscalls/linux.h>

#include <elfcalls.h>

extern struct elf_calls* elfcalls(void);

long sys_sem_open(const char* name, int oflag, int mode, int value)
{
#ifndef VARIANT_DYLD
	int ret;
	int* ptr;
	// Buffer size NAME_MAX + 2 accommodates: leading '/' prefix (1) + name (NAME_MAX) + NUL (1)
	char fixed_name[NAME_MAX + 2];

	if (!name)
		return -EINVAL;

	size_t len = strlen(name);
	if (len > NAME_MAX)
		return -ENAMETOOLONG;

	// Linux requires named semaphores to begin with a slash ('/'), whereas
	// Darwin allows bare names without a leading slash. Prepend '/' if missing.
	if (name[0] != '/')
	{
		fixed_name[0] = '/';
		memcpy(fixed_name + 1, name, len + 1);
		name = fixed_name;
	}
	
	ptr = elfcalls()->sem_open(name, oflags_bsd_to_linux(oflag), mode, value);
	
	if (!ptr)
	{
		return -errno_linux_to_bsd(elfcalls()->get_errno());
	}

	return (long) ptr;
#else
	return -ENOSYS;
#endif
}
