#include <darling/emulation/xnu_syscall/bsd/impl/process/fork.h>

#include <_libkernel_init.h>

#include <darling/emulation/common/base.h>
#include <darling/emulation/conversion/errno.h>
#include <darling/emulation/conversion/signal/duct_signals.h>
#include <darling/emulation/common/bsdthread/per_thread_wd.h>
#include <darling/emulation/xnu_syscall/bsd/impl/unistd/fchdir.h>
#include <darling/emulation/linux_premigration/linux-syscalls/linux.h>
#include <darling/emulation/common/simple.h>
#include <darling/emulation/conversion/duct_errno.h>
#include <darling/emulation/linux_premigration/elfcalls_wrapper.h>
#include <darling/emulation/other/mach/lkm.h>
#include <darling/emulation/xnu_syscall/bsd/impl/unistd/close.h>
#include <darling/emulation/common/guarded/table.h>
#include <darling/emulation/xnu_syscall/bsd/helper/misc/nofile_limit.h>
#include <darling/emulation/xnu_syscall/bsd/helper/bpf/bpf_device.h>
#include <darling/emulation/common/tsd.h>
#include <darling/emulation/xnu_syscall/bsd/impl/bsdthread/bsdthread_register.h>
#include <darling/emulation/xnu_syscall/bsd/impl/bsdthread/workq_kernreturn.h>

#include <darlingserver/rpc.h>

extern _libkernel_functions_t _libkernel_functions;

#if defined(__arm64__) || defined(__aarch64__)
static int postfork_child_wdfd = -1;
static int postfork_child_lifetime_read_fd = -1;
static bool postfork_child_pending = false;

void sys_fork_postfork_child(void)
{
	if (!postfork_child_pending)
		return;
	// Consume before calling out: repeated/reentrant completion must not check
	// in twice or close a descriptor that has since been reused.
	postfork_child_pending = false;
	guard_entry_options_t options;
	options.close = __dserver_close_socket;
	guard_table_add(__dserver_per_thread_socket(), guard_flag_prevent_close | guard_flag_close_on_fork, &options);

	if (postfork_child_lifetime_read_fd != -1)
	{
		options.close = __dserver_close_process_lifetime_pipe;
		guard_table_add(__dserver_get_process_lifetime_pipe(), guard_flag_prevent_close | guard_flag_close_on_fork, &options);
	}

	int dummy_stack_variable;
	if (dserver_rpc_checkin(true, &dummy_stack_variable, postfork_child_lifetime_read_fd) < 0) {
		__simple_printf("Failed to checkin with darlingserver after fork\n");
		__simple_abort();
	}
	if (bsdthread_registered())
		set_thread_handles(_pthread_getspecific_direct(_PTHREAD_TSD_SLOT_PTHREAD_SELF));
	if (postfork_child_wdfd >= 0)
		sys_fchdir(postfork_child_wdfd);

	__dserver_close_process_lifetime_pipe(postfork_child_lifetime_read_fd);
	postfork_child_wdfd = -1;
	postfork_child_lifetime_read_fd = -1;
}
#endif

#if defined(__x86_64__) || defined(__i386__)
// The ___fork stub reserves stack space of its own before it enters
// __darling_bsd_syscall, so this function is not entered with the stack
// alignment the ABI promises. The host libc's fork() may rely on it.
__attribute__((force_align_arg_pointer))
#endif
long sys_fork(void)
{
	int ret;
	int wdfd = get_perthread_wd();

	// Fork through the host libc rather than with a raw syscall. Its fork()
	// runs the atfork handlers of host libraries, takes the host allocator's
	// locks so that the child does not inherit them held by threads that no
	// longer exist, and updates the thread state the host libc keeps (such as
	// the cached thread ID). The child code below and the RPC socket refresh
	// in the loader both use the host allocator.
	ret = native_fork();
	if (ret < 0)
		ret = errno_linux_to_bsd(ret);
	else if (ret == 0)
	{
		// in the child

		// Yama's ptrace exception is not inherited, and darlingserver is not
		// our ancestor, so without it the server cannot read or write our
		// memory (process_vm_*: EPERM). libc's fork() renews it later in
		// mach_driver_init, but posix_spawn's child goes straight to execve,
		// whose first RPC already needs it.
		LINUX_SYSCALL(__NR_prctl, 0x59616d61 /* PR_SET_PTRACER */, (unsigned long)-1 /* PR_SET_PTRACER_ANY */, 0UL, 0UL, 0UL);

		// the old RPC FD will be closed in `guard_table_postfork_child`;
		// we don't need to close it ourselves.
		// that should also take care of closing descriptors for any other threads.
		guard_table_postfork_child();
		nofile_limit_postfork_child();
		bpf_device_postfork_child();
		workq_postfork_child();

		// create a new dserver RPC socket
		__dserver_per_thread_socket_refresh();
		int newReadFd = __dserver_process_lifetime_pipe_refresh();

#if defined(__arm64__) || defined(__aarch64__)
		// Modern malloc keeps every xzone list fork-locked until its child
		// callback. Defer work that can allocate until libSystem unlocks malloc.
		postfork_child_wdfd = wdfd;
		postfork_child_lifetime_read_fd = newReadFd;
		postfork_child_pending = true;
#else
		// guard it
		guard_entry_options_t options;
		options.close = __dserver_close_socket;
		guard_table_add(__dserver_per_thread_socket(), guard_flag_prevent_close | guard_flag_close_on_fork, &options);

		// only guard the lifetime pipe if it's used.
		if (newReadFd != -1)
		{
			options.close = __dserver_close_process_lifetime_pipe;
			guard_table_add(__dserver_get_process_lifetime_pipe(), guard_flag_prevent_close | guard_flag_close_on_fork, &options);
		}

		int dummy_stack_variable;
		if (dserver_rpc_checkin(true, &dummy_stack_variable, newReadFd) < 0) {
			// we can't do ANYTHING if darlingserver fails to acknowledge us
			__simple_printf("Failed to checkin with darlingserver after fork\n");
			__simple_abort();
		}
		if (bsdthread_registered())
			set_thread_handles(_pthread_getspecific_direct(_PTHREAD_TSD_SLOT_PTHREAD_SELF));
		if (wdfd >= 0)
			sys_fchdir(wdfd);

		__dserver_close_process_lifetime_pipe(newReadFd);
#endif
	}
	else
	{
		// The child inherits our special ports (e.g. the bootstrap port) when it
		// checks in with darlingserver, so we must not return before that.
		// Only a fork that created a child waits: no child checks in after a
		// failed one.
		while (dserver_rpc_fork_wait_for_child() == -LINUX_EINTR)
			;
	}

	return ret;
}
