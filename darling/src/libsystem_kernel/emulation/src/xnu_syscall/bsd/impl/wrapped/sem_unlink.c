#include <darling/emulation/xnu_syscall/bsd/impl/wrapped/sem_unlink.h>
#include <sys/errno.h>
#include <string.h>
#include <limits.h>

#include <darling/emulation/common/base.h>
#include <darling/emulation/conversion/errno.h>
#include <darling/emulation/linux_premigration/linux-syscalls/linux.h>

#include <elfcalls.h>

extern struct elf_calls* elfcalls(void);

long sys_sem_unlink(const char* name)
{
#ifndef VARIANT_DYLD
	int ret;
	// Buffer size NAME_MAX + 2 accommodates: leading '/' prefix (1) + name (NAME_MAX) + NUL (1)
	char fixed_name[NAME_MAX + 2];

	if (!name)
		return -EINVAL;

	size_t len = strlen(name);
	if (len > NAME_MAX)
		return -ENAMETOOLONG;

	if (name[0] != '/')
	{
		fixed_name[0] = '/';
		memcpy(fixed_name + 1, name, len + 1);
		name = fixed_name;
	}

	ret = elfcalls()->sem_unlink(name);
	if (ret == -1)
		ret = -errno_linux_to_bsd(elfcalls()->get_errno());

	return ret;
#else
	return -ENOSYS;
#endif
}
