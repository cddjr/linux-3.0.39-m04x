/*
 *  linux/arch/arm/kernel/traps.c
 *
 *  Copyright (C) 1995-2009 Russell King
 *  Fragments that appear the same as linux/arch/i386/kernel/traps.c (C) Linus Torvalds
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  'traps.c' handles hardware exceptions after we have saved some state in
 *  'linux/arch/arm/lib/traps.S'.  Mostly a debugging aid, but will probably
 *  kill the offending process.
 */
#include <linux/signal.h>
#include <linux/personality.h>
#include <linux/kallsyms.h>
#include <linux/spinlock.h>
#include <linux/uaccess.h>
#include <linux/hardirq.h>
#include <linux/kdebug.h>
#include <linux/module.h>
#include <linux/kexec.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/fs.h>

#include <asm/atomic.h>
#include <asm/cacheflush.h>
#include <asm/system.h>
#include <asm/unistd.h>
#include <asm/traps.h>
#include <asm/unwind.h>
#include <asm/tls.h>
#ifdef CONFIG_MX_RECOVERY_KERNEL
#include "mx_recovery_key.h"
#endif
#include "signal.h"


static const char *handler[]= { "prefetch abort", "data abort", "address exception", "interrupt" };

void *vectors_page;

#ifdef CONFIG_DEBUG_USER
unsigned int user_debug;

static int __init user_debug_setup(char *str)
{
	get_option(&str, &user_debug);
	return 1;
}
__setup("user_debug=", user_debug_setup);
#endif

static void dump_mem(const char *, const char *, unsigned long, unsigned long);

void dump_backtrace_entry(unsigned long where, unsigned long from, unsigned long frame)
{
#ifdef CONFIG_KALLSYMS
	printk("[<%08lx>] (%pS) from [<%08lx>] (%pS)\n", where, (void *)where, from, (void *)from);
#else
	printk("Function entered at [<%08lx>] from [<%08lx>]\n", where, from);
#endif

	if (in_exception_text(where))
		dump_mem("", "Exception stack", frame + 4, frame + 4 + sizeof(struct pt_regs));
}

#ifndef CONFIG_ARM_UNWIND
/*
 * Stack pointers should always be within the kernels view of
 * physical memory.  If it is not there, then we can't dump
 * out any information relating to the stack.
 */
static int verify_stack(unsigned long sp)
{
	if (sp < PAGE_OFFSET ||
	    (sp > (unsigned long)high_memory && high_memory != NULL))
		return -EFAULT;

	return 0;
}
#endif

/*
 * Dump out the contents of some memory nicely...
 */
static void dump_mem(const char *lvl, const char *str, unsigned long bottom,
		     unsigned long top)
{
	unsigned long first;
	mm_segment_t fs;
	int i;

	/*
	 * We need to switch to kernel mode so that we can use __get_user
	 * to safely read from kernel space.  Note that we now dump the
	 * code first, just in case the backtrace kills us.
	 */
	fs = get_fs();
	set_fs(KERNEL_DS);

	printk("%s%s(0x%08lx to 0x%08lx)\n", lvl, str, bottom, top);

	for (first = bottom & ~31; first < top; first += 32) {
		unsigned long p;
		char str[sizeof(" 12345678") * 8 + 1];

		memset(str, ' ', sizeof(str));
		str[sizeof(str) - 1] = '\0';

		for (p = first, i = 0; i < 8 && p < top; i++, p += 4) {
			if (p >= bottom && p < top) {
				unsigned long val;
				if (__get_user(val, (unsigned long *)p) == 0)
					sprintf(str + i * 9, " %08lx", val);
				else
					sprintf(str + i * 9, " ????????");
			}
		}
		printk("%s%04lx:%s\n", lvl, first & 0xffff, str);
	}

	set_fs(fs);
}

static void dump_instr(const char *lvl, struct pt_regs *regs)
{
	unsigned long addr = instruction_pointer(regs);
	const int thumb = thumb_mode(regs);
	const int width = thumb ? 4 : 8;
	mm_segment_t fs;
	char str[sizeof("00000000 ") * 5 + 2 + 1], *p = str;
	int i;

	/*
	 * We need to switch to kernel mode so that we can use __get_user
	 * to safely read from kernel space.  Note that we now dump the
	 * code first, just in case the backtrace kills us.
	 */
	fs = get_fs();
	set_fs(KERNEL_DS);

	for (i = -4; i < 1 + !!thumb; i++) {
		unsigned int val, bad;

		if (thumb)
			bad = __get_user(val, &((u16 *)addr)[i]);
		else
			bad = __get_user(val, &((u32 *)addr)[i]);

		if (!bad)
			p += sprintf(p, i == 0 ? "(%0*x) " : "%0*x ",
					width, val);
		else {
			p += sprintf(p, "bad PC value");
			break;
		}
	}
	printk("%sCode: %s\n", lvl, str);

	set_fs(fs);
}

#ifdef CONFIG_ARM_UNWIND
static inline void dump_backtrace(struct pt_regs *regs, struct task_struct *tsk)
{
	unwind_backtrace(regs, tsk);
}
#else
static void dump_backtrace(struct pt_regs *regs, struct task_struct *tsk)
{
	unsigned int fp, mode;
	int ok = 1;

	printk("Backtrace: ");

	if (!tsk)
		tsk = current;

	if (regs) {
		fp = regs->ARM_fp;
		mode = processor_mode(regs);
	} else if (tsk != current) {
		fp = thread_saved_fp(tsk);
		mode = 0x10;
	} else {
		asm("mov %0, fp" : "=r" (fp) : : "cc");
		mode = 0x10;
	}

	if (!fp) {
		printk("no frame pointer");
		ok = 0;
	} else if (verify_stack(fp)) {
		printk("invalid frame pointer 0x%08x", fp);
		ok = 0;
	} else if (fp < (unsigned long)end_of_stack(tsk))
		printk("frame pointer underflow");
	printk("\n");

	if (ok)
		c_backtrace(fp, mode);
}
#endif

void dump_stack(void)
{
	dump_backtrace(NULL, NULL);
}

EXPORT_SYMBOL(dump_stack);

void show_stack(struct task_struct *tsk, unsigned long *sp)
{
	dump_backtrace(NULL, tsk);
	barrier();
}

#ifdef CONFIG_PREEMPT
#define S_PREEMPT " PREEMPT"
#else
#define S_PREEMPT ""
#endif
#ifdef CONFIG_SMP
#define S_SMP " SMP"
#else
#define S_SMP ""
#endif

static int __die(const char *str, int err, struct thread_info *thread, struct pt_regs *regs)
{
	struct task_struct *tsk = thread->task;
	static int die_counter;
	int ret;

	printk(KERN_EMERG "Internal error: %s: %x [#%d]" S_PREEMPT S_SMP "\n",
	       str, err, ++die_counter);

	/* trap and error numbers are mostly meaningless on ARM */
	ret = notify_die(DIE_OOPS, str, regs, err, tsk->thread.trap_no, SIGSEGV);
	if (ret == NOTIFY_STOP)
		return ret;

	print_modules();
	__show_regs(regs);
	printk(KERN_EMERG "Process %.*s (pid: %d, stack limit = 0x%p)\n",
		TASK_COMM_LEN, tsk->comm, task_pid_nr(tsk), thread + 1);

	if (!user_mode(regs) || in_interrupt()) {
		dump_mem(KERN_EMERG, "Stack: ", regs->ARM_sp,
			 THREAD_SIZE + (unsigned long)task_stack_page(tsk));
		dump_backtrace(regs, tsk);
		dump_instr(KERN_EMERG, regs);
	}

	return ret;
}

static DEFINE_SPINLOCK(die_lock);

/*
 * This function is protected against re-entrancy.
 */
void die(const char *str, struct pt_regs *regs, int err)
{
	struct thread_info *thread = current_thread_info();
	int ret;

	oops_enter();

	spin_lock_irq(&die_lock);
	console_verbose();
	bust_spinlocks(1);
	ret = __die(str, err, thread, regs);

	if (regs && kexec_should_crash(thread->task))
		crash_kexec(regs);

	bust_spinlocks(0);
	add_taint(TAINT_DIE);
	spin_unlock_irq(&die_lock);
	oops_exit();

	if (in_interrupt())
		panic("Fatal exception in interrupt");
	if (panic_on_oops)
		panic("Fatal exception");
	if (ret != NOTIFY_STOP)
		do_exit(SIGSEGV);
}

void arm_notify_die(const char *str, struct pt_regs *regs,
		struct siginfo *info, unsigned long err, unsigned long trap)
{
	if (user_mode(regs)) {
		current->thread.error_code = err;
		current->thread.trap_no = trap;

		force_sig_info(info->si_signo, info, current);
	} else {
		die(str, regs, err);
	}
}

static LIST_HEAD(undef_hook);
static DEFINE_SPINLOCK(undef_lock);

void register_undef_hook(struct undef_hook *hook)
{
	unsigned long flags;

	spin_lock_irqsave(&undef_lock, flags);
	list_add(&hook->node, &undef_hook);
	spin_unlock_irqrestore(&undef_lock, flags);
}

void unregister_undef_hook(struct undef_hook *hook)
{
	unsigned long flags;

	spin_lock_irqsave(&undef_lock, flags);
	list_del(&hook->node);
	spin_unlock_irqrestore(&undef_lock, flags);
}

static int call_undef_hook(struct pt_regs *regs, unsigned int instr)
{
	struct undef_hook *hook;
	unsigned long flags;
	int (*fn)(struct pt_regs *regs, unsigned int instr) = NULL;

	spin_lock_irqsave(&undef_lock, flags);
	list_for_each_entry(hook, &undef_hook, node)
		if ((instr & hook->instr_mask) == hook->instr_val &&
		    (regs->ARM_cpsr & hook->cpsr_mask) == hook->cpsr_val)
			fn = hook->fn;
	spin_unlock_irqrestore(&undef_lock, flags);

	return fn ? fn(regs, instr) : 1;
}

asmlinkage void __exception do_undefinstr(struct pt_regs *regs)
{
	unsigned int correction = thumb_mode(regs) ? 2 : 4;
	unsigned int instr;
	siginfo_t info;
	void __user *pc;

	/*
	 * According to the ARM ARM, PC is 2 or 4 bytes ahead,
	 * depending whether we're in Thumb mode or not.
	 * Correct this offset.
	 */
	regs->ARM_pc -= correction;

	pc = (void __user *)instruction_pointer(regs);

	if (processor_mode(regs) == SVC_MODE) {
		instr = *(u32 *) pc;
	} else if (thumb_mode(regs)) {
		get_user(instr, (u16 __user *)pc);
	} else {
		get_user(instr, (u32 __user *)pc);
	}

	if (call_undef_hook(regs, instr) == 0)
		return;

#ifdef CONFIG_DEBUG_USER
	if (user_debug & UDBG_UNDEFINED) {
		printk(KERN_INFO "%s (%d): undefined instruction: pc=%p\n",
			current->comm, task_pid_nr(current), pc);
		dump_instr(KERN_INFO, regs);
	}
#endif

	info.si_signo = SIGILL;
	info.si_errno = 0;
	info.si_code  = ILL_ILLOPC;
	info.si_addr  = pc;

	arm_notify_die("Oops - undefined instruction", regs, &info, 0, 6);
}

asmlinkage void do_unexp_fiq (struct pt_regs *regs)
{
	printk("Hmm.  Unexpected FIQ received, but trying to continue\n");
	printk("You may have a hardware problem...\n");
}

/*
 * bad_mode handles the impossible case in the vectors.  If you see one of
 * these, then it's extremely serious, and could mean you have buggy hardware.
 * It never returns, and never tries to sync.  We hope that we can at least
 * dump out some state information...
 */
asmlinkage void bad_mode(struct pt_regs *regs, int reason)
{
	console_verbose();

	printk(KERN_CRIT "Bad mode in %s handler detected\n", handler[reason]);

	die("Oops - bad mode", regs, 0);
	local_irq_disable();
	panic("bad mode");
}

static int bad_syscall(int n, struct pt_regs *regs)
{
	struct thread_info *thread = current_thread_info();
	siginfo_t info;

	if ((current->personality & PER_MASK) != PER_LINUX &&
	    thread->exec_domain->handler) {
		thread->exec_domain->handler(n, regs);
		return regs->ARM_r0;
	}

#ifdef CONFIG_DEBUG_USER
	if (user_debug & UDBG_SYSCALL) {
		printk(KERN_ERR "[%d] %s: obsolete system call %08x.\n",
			task_pid_nr(current), current->comm, n);
		dump_instr(KERN_ERR, regs);
	}
#endif

	info.si_signo = SIGILL;
	info.si_errno = 0;
	info.si_code  = ILL_ILLTRP;
	info.si_addr  = (void __user *)instruction_pointer(regs) -
			 (thumb_mode(regs) ? 2 : 4);

	arm_notify_die("Oops - bad syscall", regs, &info, n, 0);

	return regs->ARM_r0;
}

static inline void
do_cache_op(unsigned long start, unsigned long end, int flags)
{
	struct mm_struct *mm = current->active_mm;
	struct vm_area_struct *vma;

	if (end < start || flags)
		return;

	down_read(&mm->mmap_sem);
	vma = find_vma(mm, start);
	if (vma && vma->vm_start < end) {
		if (start < vma->vm_start)
			start = vma->vm_start;
		if (end > vma->vm_end)
			end = vma->vm_end;

		up_read(&mm->mmap_sem);
		flush_cache_user_range(start, end);
		return;
	}
	up_read(&mm->mmap_sem);
}


#if defined(CONFIG_MX_SERIAL_TYPE) || defined(CONFIG_MX2_SERIAL_TYPE)
#define HASH_DIGEST_SIZE		16
#define HASH_HMAC_BLOCK_SIZE	64
#define HASH_BLOCK_WORDS		16
#define HASH_HASH_WORDS		4

#define F1(x, y, z)	(z ^ (x & (y ^ z)))
#define F2(x, y, z)	F1(z, x, y)
#define F3(x, y, z)	(x ^ y ^ z)
#define F4(x, y, z)	(y ^ (x | ~z))

#define HASHSTEP(f, w, x, y, z, in, s) \
	(w += f(x, y, z) + in, w = (w<<s | w>>(32-s)) + x)

struct verify_ctx {
	u32 hash[HASH_HASH_WORDS];
	u32 block[HASH_BLOCK_WORDS];
	u32 byte_count;
};

static void verify_transform(u32 *hash, u32 const *in)
{
	u32 a, b, c, d;

	hash[0] = 0x67452301;
	hash[1] = 0xefcdab89;
	hash[2] = 0x98badcfe;
	hash[3] = 0x10325476;

	a = hash[0];
	b = hash[1];
	c = hash[2];
	d = hash[3];

	HASHSTEP(F1, a, b, c, d, in[0] + 0xd76aa478, 7);
	HASHSTEP(F1, d, a, b, c, in[1] + 0xe8c7b756, 12);
	HASHSTEP(F1, c, d, a, b, in[2] + 0x242070db, 17);
	HASHSTEP(F1, b, c, d, a, in[3] + 0xc1bdceee, 22);
	HASHSTEP(F1, a, b, c, d, in[4] + 0xf57c0faf, 7);
	HASHSTEP(F1, d, a, b, c, in[5] + 0x4787c62a, 12);
	HASHSTEP(F1, c, d, a, b, in[6] + 0xa8304613, 17);
	HASHSTEP(F1, b, c, d, a, in[7] + 0xfd469501, 22);
	HASHSTEP(F1, a, b, c, d, in[8] + 0x698098d8, 7);
	HASHSTEP(F1, d, a, b, c, in[9] + 0x8b44f7af, 12);
	HASHSTEP(F1, c, d, a, b, in[10] + 0xffff5bb1, 17);
	HASHSTEP(F1, b, c, d, a, in[11] + 0x895cd7be, 22);
	HASHSTEP(F1, a, b, c, d, in[12] + 0x6b901122, 7);
	HASHSTEP(F1, d, a, b, c, in[13] + 0xfd987193, 12);
	HASHSTEP(F1, c, d, a, b, in[14] + 0xa679438e, 17);
	HASHSTEP(F1, b, c, d, a, in[15] + 0x49b40821, 22);

	HASHSTEP(F2, a, b, c, d, in[1] + 0xf61e2562, 5);
	HASHSTEP(F2, d, a, b, c, in[6] + 0xc040b340, 9);
	HASHSTEP(F2, c, d, a, b, in[11] + 0x265e5a51, 14);
	HASHSTEP(F2, b, c, d, a, in[0] + 0xe9b6c7aa, 20);
	HASHSTEP(F2, a, b, c, d, in[5] + 0xd62f105d, 5);
	HASHSTEP(F2, d, a, b, c, in[10] + 0x02441453, 9);
	HASHSTEP(F2, c, d, a, b, in[15] + 0xd8a1e681, 14);
	HASHSTEP(F2, b, c, d, a, in[4] + 0xe7d3fbc8, 20);
	HASHSTEP(F2, a, b, c, d, in[9] + 0x21e1cde6, 5);
	HASHSTEP(F2, d, a, b, c, in[14] + 0xc33707d6, 9);
	HASHSTEP(F2, c, d, a, b, in[3] + 0xf4d50d87, 14);
	HASHSTEP(F2, b, c, d, a, in[8] + 0x455a14ed, 20);
	HASHSTEP(F2, a, b, c, d, in[13] + 0xa9e3e905, 5);
	HASHSTEP(F2, d, a, b, c, in[2] + 0xfcefa3f8, 9);
	HASHSTEP(F2, c, d, a, b, in[7] + 0x676f02d9, 14);
	HASHSTEP(F2, b, c, d, a, in[12] + 0x8d2a4c8a, 20);

	HASHSTEP(F3, a, b, c, d, in[5] + 0xfffa3942, 4);
	HASHSTEP(F3, d, a, b, c, in[8] + 0x8771f681, 11);
	HASHSTEP(F3, c, d, a, b, in[11] + 0x6d9d6122, 16);
	HASHSTEP(F3, b, c, d, a, in[14] + 0xfde5380c, 23);
	HASHSTEP(F3, a, b, c, d, in[1] + 0xa4beea44, 4);
	HASHSTEP(F3, d, a, b, c, in[4] + 0x4bdecfa9, 11);
	HASHSTEP(F3, c, d, a, b, in[7] + 0xf6bb4b60, 16);
	HASHSTEP(F3, b, c, d, a, in[10] + 0xbebfbc70, 23);
	HASHSTEP(F3, a, b, c, d, in[13] + 0x289b7ec6, 4);
	HASHSTEP(F3, d, a, b, c, in[0] + 0xeaa127fa, 11);
	HASHSTEP(F3, c, d, a, b, in[3] + 0xd4ef3085, 16);
	HASHSTEP(F3, b, c, d, a, in[6] + 0x04881d05, 23);
	HASHSTEP(F3, a, b, c, d, in[9] + 0xd9d4d039, 4);
	HASHSTEP(F3, d, a, b, c, in[12] + 0xe6db99e5, 11);
	HASHSTEP(F3, c, d, a, b, in[15] + 0x1fa27cf8, 16);
	HASHSTEP(F3, b, c, d, a, in[2] + 0xc4ac5665, 23);

	HASHSTEP(F4, a, b, c, d, in[0] + 0xf4292244, 6);
	HASHSTEP(F4, d, a, b, c, in[7] + 0x432aff97, 10);
	HASHSTEP(F4, c, d, a, b, in[14] + 0xab9423a7, 15);
	HASHSTEP(F4, b, c, d, a, in[5] + 0xfc93a039, 21);
	HASHSTEP(F4, a, b, c, d, in[12] + 0x655b59c3, 6);
	HASHSTEP(F4, d, a, b, c, in[3] + 0x8f0ccc92, 10);
	HASHSTEP(F4, c, d, a, b, in[10] + 0xffeff47d, 15);
	HASHSTEP(F4, b, c, d, a, in[1] + 0x85845dd1, 21);
	HASHSTEP(F4, a, b, c, d, in[8] + 0x6fa87e4f, 6);
	HASHSTEP(F4, d, a, b, c, in[15] + 0xfe2ce6e0, 10);
	HASHSTEP(F4, c, d, a, b, in[6] + 0xa3014314, 15);
	HASHSTEP(F4, b, c, d, a, in[13] + 0x4e0811a1, 21);
	HASHSTEP(F4, a, b, c, d, in[4] + 0xf7537e82, 6);
	HASHSTEP(F4, d, a, b, c, in[11] + 0xbd3af235, 10);
	HASHSTEP(F4, c, d, a, b, in[2] + 0x2ad7d2bb, 15);
	HASHSTEP(F4, b, c, d, a, in[9] + 0xeb86d391, 21);

	hash[0] += a;
	hash[1] += b;
	hash[2] += c;
	hash[3] += d;
}

static int machine_verify( struct verify_ctx __user *ctx_info)
{
	struct verify_ctx ctx;

	memset(&ctx, 0, sizeof(ctx));

	if (!copy_from_user(&ctx, ctx_info, sizeof(ctx))) {
		verify_transform(ctx.hash, ctx.block);
		if(copy_to_user(ctx_info, &ctx, sizeof(ctx)))
		{
			return  -EFAULT;
		}
		return 0;
	}
	return  -EFAULT;
}

struct machine_info {
	char mfg[32];
	char prod[32];
	char sn[32];
} g_info;

extern int android_usb_set_machine_info(char *mfg, char *prod, char *sn);

static int set_machine_info( struct machine_info __user *info)
{
	struct machine_info minfo;

	memset(&minfo, 0, sizeof(minfo));

#if defined(CONFIG_USB_ANDROID) || defined(CONFIG_USB_G_ANDROID)
	if (!copy_from_user(&minfo, info, sizeof(minfo))) {
		g_info = minfo;
		return android_usb_set_machine_info(g_info.mfg,  g_info.prod, g_info.sn);
	}
#endif

	return  -EFAULT;
}

static int get_recovery_key(__user char *key)
{
#ifdef CONFIG_MX_RECOVERY_KERNEL
	if(copy_to_user(key, meizu_recovery_key, sizeof(meizu_recovery_key))) {
		return  -EFAULT;
	}
#endif
	return 0;
}

enum {
	CMD_READ,
	CMD_PREPARE,
	CMD_WRITE
};

static DEFINE_MUTEX(private_entry_mutex);

extern int private_entry_read(int slot, __user char *out_buf);
extern int private_entry_write_prepare(int slot, __user char *random_buf);
extern int private_entry_write(int slot, __user char *in_buf);

static int do_private_entry(int slot, int cmd , __user char *in_buf, __user char *out_buf)
{
	int err = 0;
	pr_info("slot %d cmd %d %p %p\n", slot, cmd, in_buf, out_buf);
	mutex_lock(&private_entry_mutex);

	switch(cmd) {
		case CMD_READ:
			if (private_entry_read(slot, out_buf)) {
				err =   -EFAULT;
				goto out;
			}
			break;
		case CMD_PREPARE:
			if (private_entry_write_prepare(slot, out_buf)) {
				err =   -EFAULT;
				goto out;
			}
			break;
		case CMD_WRITE:
			if (private_entry_write(slot, in_buf)) {
				err =   -EFAULT;
				goto out;
			}
			break;
		default:
			err = -EINVAL;
			goto out;
	}

out:
	mutex_unlock(&private_entry_mutex);
	return err;
}

enum {
	SYSTEM_READ,
	SYSTEM_WRITE
};

extern int system_data_func(int cmd , __user char *buf, int size);

static int do_system_data(int cmd , __user char *buf, int size)
{
	int err = 0;
	pr_debug("%s %d %d\n", __func__, cmd, size);
	mutex_lock(&private_entry_mutex);

	switch(cmd) {
		case SYSTEM_READ:
		case SYSTEM_WRITE:
			if (system_data_func(cmd, buf, size)) {
				err =   -EFAULT;
				goto out;
			}
			break;
		default:
			err = -EINVAL;
			goto out;
	}

out:
	mutex_unlock(&private_entry_mutex);
	return err;
}
#endif//CONFIG_MX_SERIAL_TYPE
/*
 * Handle all unrecognised system calls.
 *  0x9f0000 - 0x9fffff are some more esoteric system calls
 */
#define NR(x) ((__ARM_NR_##x) - __ARM_NR_BASE)
asmlinkage int arm_syscall(int no, struct pt_regs *regs)
{
	struct thread_info *thread = current_thread_info();
	siginfo_t info;

	if ((no >> 16) != (__ARM_NR_BASE>> 16))
		return bad_syscall(no, regs);

	switch (no & 0xffff) {
	case 0: /* branch through 0 */
		info.si_signo = SIGSEGV;
		info.si_errno = 0;
		info.si_code  = SEGV_MAPERR;
		info.si_addr  = NULL;

		arm_notify_die("branch through zero", regs, &info, 0, 0);
		return 0;

	case NR(breakpoint): /* SWI BREAK_POINT */
		regs->ARM_pc -= thumb_mode(regs) ? 2 : 4;
		ptrace_break(current, regs);
		return regs->ARM_r0;

	/*
	 * Flush a region from virtual address 'r0' to virtual address 'r1'
	 * _exclusive_.  There is no alignment requirement on either address;
	 * user space does not need to know the hardware cache layout.
	 *
	 * r2 contains flags.  It should ALWAYS be passed as ZERO until it
	 * is defined to be something else.  For now we ignore it, but may
	 * the fires of hell burn in your belly if you break this rule. ;)
	 *
	 * (at a later date, we may want to allow this call to not flush
	 * various aspects of the cache.  Passing '0' will guarantee that
	 * everything necessary gets flushed to maintain consistency in
	 * the specified region).
	 */
	case NR(cacheflush):
		do_cache_op(regs->ARM_r0, regs->ARM_r1, regs->ARM_r2);
		return 0;

	case NR(usr26):
		if (!(elf_hwcap & HWCAP_26BIT))
			break;
		regs->ARM_cpsr &= ~MODE32_BIT;
		return regs->ARM_r0;

	case NR(usr32):
		if (!(elf_hwcap & HWCAP_26BIT))
			break;
		regs->ARM_cpsr |= MODE32_BIT;
		return regs->ARM_r0;

	case NR(set_tls):
		thread->tp_value = regs->ARM_r0;
		if (tls_emu)
			return 0;
		if (has_tls_reg) {
			asm ("mcr p15, 0, %0, c13, c0, 3"
				: : "r" (regs->ARM_r0));
		} else {
			/*
			 * User space must never try to access this directly.
			 * Expect your app to break eventually if you do so.
			 * The user helper at 0xffff0fe0 must be used instead.
			 * (see entry-armv.S for details)
			 */
			*((unsigned int *)0xffff0ff0) = regs->ARM_r0;
		}
		return 0;

#ifdef CONFIG_NEEDS_SYSCALL_FOR_CMPXCHG
	/*
	 * Atomically store r1 in *r2 if *r2 is equal to r0 for user space.
	 * Return zero in r0 if *MEM was changed or non-zero if no exchange
	 * happened.  Also set the user C flag accordingly.
	 * If access permissions have to be fixed up then non-zero is
	 * returned and the operation has to be re-attempted.
	 *
	 * *NOTE*: This is a ghost syscall private to the kernel.  Only the
	 * __kuser_cmpxchg code in entry-armv.S should be aware of its
	 * existence.  Don't ever use this from user code.
	 */
	case NR(cmpxchg):
	for (;;) {
		extern void do_DataAbort(unsigned long addr, unsigned int fsr,
					 struct pt_regs *regs);
		unsigned long val;
		unsigned long addr = regs->ARM_r2;
		struct mm_struct *mm = current->mm;
		pgd_t *pgd; pmd_t *pmd; pte_t *pte;
		spinlock_t *ptl;

		regs->ARM_cpsr &= ~PSR_C_BIT;
		down_read(&mm->mmap_sem);
		pgd = pgd_offset(mm, addr);
		if (!pgd_present(*pgd))
			goto bad_access;
		pmd = pmd_offset(pgd, addr);
		if (!pmd_present(*pmd))
			goto bad_access;
		pte = pte_offset_map_lock(mm, pmd, addr, &ptl);
		if (!pte_present(*pte) || !pte_write(*pte) || !pte_dirty(*pte)) {
			pte_unmap_unlock(pte, ptl);
			goto bad_access;
		}
		val = *(unsigned long *)addr;
		val -= regs->ARM_r0;
		if (val == 0) {
			*(unsigned long *)addr = regs->ARM_r1;
			regs->ARM_cpsr |= PSR_C_BIT;
		}
		pte_unmap_unlock(pte, ptl);
		up_read(&mm->mmap_sem);
		return val;

		bad_access:
		up_read(&mm->mmap_sem);
		/* simulate a write access fault */
		do_DataAbort(addr, 15 + (1 << 11), regs);
	}
#endif
#if defined(CONFIG_MX_SERIAL_TYPE) || defined(CONFIG_MX2_SERIAL_TYPE)
	case NR(machine_verify):
		return machine_verify((struct verify_ctx __user *)regs->ARM_r0);
		break;
	case NR(set_machine_info):
		return set_machine_info((struct machine_info __user *)regs->ARM_r0);
		break;
	case NR(get_recovery_key):
		return get_recovery_key((__user char *)regs->ARM_r0);
		break;
	case NR(do_private_entry):
		return do_private_entry((__user int)regs->ARM_r0,
					(__user int)regs->ARM_r1, 
					(__user char *)regs->ARM_r2, 
					(__user char *)regs->ARM_r3);
		break;
	case NR(do_system_data):
		return do_system_data((__user int)regs->ARM_r0,
					(__user char *)regs->ARM_r1,
					(__user int)regs->ARM_r2); 
		break;
#endif
	default:
		/* Calls 9f00xx..9f07ff are defined to return -ENOSYS
		   if not implemented, rather than raising SIGILL.  This
		   way the calling program can gracefully determine whether
		   a feature is supported.  */
		if ((no & 0xffff) <= 0x7ff)
			return -ENOSYS;
		break;
	}
#ifdef CONFIG_DEBUG_USER
	/*
	 * experience shows that these seem to indicate that
	 * something catastrophic has happened
	 */
	if (user_debug & UDBG_SYSCALL) {
		printk("[%d] %s: arm syscall %d\n",
		       task_pid_nr(current), current->comm, no);
		dump_instr("", regs);
		if (user_mode(regs)) {
			__show_regs(regs);
			c_backtrace(regs->ARM_fp, processor_mode(regs));
		}
	}
#endif
	info.si_signo = SIGILL;
	info.si_errno = 0;
	info.si_code  = ILL_ILLTRP;
	info.si_addr  = (void __user *)instruction_pointer(regs) -
			 (thumb_mode(regs) ? 2 : 4);

	arm_notify_die("Oops - bad syscall(2)", regs, &info, no, 0);
	return 0;
}

#ifdef CONFIG_TLS_REG_EMUL

/*
 * We might be running on an ARMv6+ processor which should have the TLS
 * register but for some reason we can't use it, or maybe an SMP system
 * using a pre-ARMv6 processor (there are apparently a few prototypes like
 * that in existence) and therefore access to that register must be
 * emulated.
 */

static int get_tp_trap(struct pt_regs *regs, unsigned int instr)
{
	int reg = (instr >> 12) & 15;
	if (reg == 15)
		return 1;
	regs->uregs[reg] = current_thread_info()->tp_value;
	regs->ARM_pc += 4;
	return 0;
}

static struct undef_hook arm_mrc_hook = {
	.instr_mask	= 0x0fff0fff,
	.instr_val	= 0x0e1d0f70,
	.cpsr_mask	= PSR_T_BIT,
	.cpsr_val	= 0,
	.fn		= get_tp_trap,
};

static int __init arm_mrc_hook_init(void)
{
	register_undef_hook(&arm_mrc_hook);
	return 0;
}

late_initcall(arm_mrc_hook_init);

#endif

void __bad_xchg(volatile void *ptr, int size)
{
	printk("xchg: bad data size: pc 0x%p, ptr 0x%p, size %d\n",
		__builtin_return_address(0), ptr, size);
	BUG();
}
EXPORT_SYMBOL(__bad_xchg);

/*
 * A data abort trap was taken, but we did not handle the instruction.
 * Try to abort the user program, or panic if it was the kernel.
 */
asmlinkage void
baddataabort(int code, unsigned long instr, struct pt_regs *regs)
{
	unsigned long addr = instruction_pointer(regs);
	siginfo_t info;

#ifdef CONFIG_DEBUG_USER
	if (user_debug & UDBG_BADABORT) {
		printk(KERN_ERR "[%d] %s: bad data abort: code %d instr 0x%08lx\n",
			task_pid_nr(current), current->comm, code, instr);
		dump_instr(KERN_ERR, regs);
		show_pte(current->mm, addr);
	}
#endif

	info.si_signo = SIGILL;
	info.si_errno = 0;
	info.si_code  = ILL_ILLOPC;
	info.si_addr  = (void __user *)addr;

	arm_notify_die("unknown data abort code", regs, &info, instr, 0);
}

void __attribute__((noreturn)) __bug(const char *file, int line)
{
	printk(KERN_CRIT"kernel BUG at %s:%d!\n", file, line);
	*(int *)0 = 0;

	/* Avoid "noreturn function does return" */
	for (;;);
}
EXPORT_SYMBOL(__bug);

void __readwrite_bug(const char *fn)
{
	printk("%s called, but not implemented\n", fn);
	BUG();
}
EXPORT_SYMBOL(__readwrite_bug);

void __pte_error(const char *file, int line, pte_t pte)
{
	printk("%s:%d: bad pte %08llx.\n", file, line, (long long)pte_val(pte));
}

void __pmd_error(const char *file, int line, pmd_t pmd)
{
	printk("%s:%d: bad pmd %08llx.\n", file, line, (long long)pmd_val(pmd));
}

void __pgd_error(const char *file, int line, pgd_t pgd)
{
	printk("%s:%d: bad pgd %08llx.\n", file, line, (long long)pgd_val(pgd));
}

asmlinkage void __div0(void)
{
	printk("Division by zero in kernel.\n");
	dump_stack();
}
EXPORT_SYMBOL(__div0);

void abort(void)
{
	BUG();

	/* if that doesn't kill us, halt */
	panic("Oops failed to kill thread");
}
EXPORT_SYMBOL(abort);

void __init trap_init(void)
{
	return;
}

static void __init kuser_get_tls_init(unsigned long vectors)
{
	/*
	 * vectors + 0xfe0 = __kuser_get_tls
	 * vectors + 0xfe8 = hardware TLS instruction at 0xffff0fe8
	 */
	if (tls_emu || has_tls_reg)
		memcpy((void *)vectors + 0xfe0, (void *)vectors + 0xfe8, 4);
}

void __init early_trap_init(void)
{
#if defined(CONFIG_CPU_USE_DOMAINS)
	unsigned long vectors = CONFIG_VECTORS_BASE;
#else
	unsigned long vectors = (unsigned long)vectors_page;
#endif
	extern char __stubs_start[], __stubs_end[];
	extern char __vectors_start[], __vectors_end[];
	extern char __kuser_helper_start[], __kuser_helper_end[];
	int kuser_sz = __kuser_helper_end - __kuser_helper_start;

	/*
	 * Copy the vectors, stubs and kuser helpers (in entry-armv.S)
	 * into the vector page, mapped at 0xffff0000, and ensure these
	 * are visible to the instruction stream.
	 */
	memcpy((void *)vectors, __vectors_start, __vectors_end - __vectors_start);
	memcpy((void *)vectors + 0x200, __stubs_start, __stubs_end - __stubs_start);
	memcpy((void *)vectors + 0x1000 - kuser_sz, __kuser_helper_start, kuser_sz);

	/*
	 * Do processor specific fixups for the kuser helpers
	 */
	kuser_get_tls_init(vectors);

	/*
	 * Copy signal return handlers into the vector page, and
	 * set sigreturn to be a pointer to these.
	 */
	memcpy((void *)(vectors + KERN_SIGRETURN_CODE - CONFIG_VECTORS_BASE),
	       sigreturn_codes, sizeof(sigreturn_codes));
	memcpy((void *)(vectors + KERN_RESTART_CODE - CONFIG_VECTORS_BASE),
	       syscall_restart_code, sizeof(syscall_restart_code));

	flush_icache_range(vectors, vectors + PAGE_SIZE);
	modify_domain(DOMAIN_USER, DOMAIN_CLIENT);
}
