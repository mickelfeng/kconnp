/*
 * The codes of this file is brought from kernel, and changed it to appreciate the lkm
 * 
 */

#ifndef _UTIL_H
#define _UTIL_H

#include <linux/version.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/net.h>
#include <net/sock.h>
#include <linux/stat.h>
#include <linux/sched.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 32)
#include <linux/fdtable.h>
#endif

#include <asm/atomic.h>
#include <linux/slab.h>
#include <linux/rcupdate.h>
#include <linux/list.h>
#include <linux/poll.h>

#define E_EVENTS (POLLERR|POLLHUP|POLLNVAL)

#define NOW_SECS (CURRENT_TIME_SEC.tv_sec)

#define NOTIFY_SIG (SIGINT)
#define SEND_FORCE (1)
#define notify(tskp) send_sig(NOTIFY_SIG, (tskp), SEND_FORCE)

#define INVOKED_BY_TGROUP_LEADER() (current == current->group_leader)

#define lkmalloc(size) kzalloc(size, GFP_ATOMIC)
#define lkmfree(ptr) kfree(ptr)

#define BYTES_ALIGN(size) (((size) + (sizeof(long) - 1)) & ~(sizeof(long) - 1))

#define SOCK_CLIENT_TAG (1 << 31)

#define IS_CLIENT_SOCK(sock) \
    ((sock)->file->f_flags & SOCK_CLIENT_TAG)

#define SET_CLIENT_FLAG(sock)   \
    ((sock)->file->f_flags |= SOCK_CLIENT_TAG)

#define CLEAR_CLIENT_FLAG(sock) \
    ((sock)->file->f_flags &= ~SOCK_CLIENT_TAG)

#define SK_ESTABLISHED(sk)  \
    (sk->sk_state == TCP_ESTABLISHED)

#define SOCK_ESTABLISHED(sock) \
    ((sock)->sk && SK_ESTABLISHED((sock)->sk))

#define IS_TCP_SOCK(sock) \
    ((sock)->type == SOCK_STREAM)


#define lkm_atomic_read(v) atomic_read((atomic_t *)v) 
#define lkm_atomic_add(v, a) atomic_add_return(a, (atomic_t *)v)
#define lkm_atomic_sub(v, a) atomic_sub_return(a, (atomic_t *)v)
#define lkm_atomic_set(v, a) atomic_set((atomic_t *)v, a) 

#define TASK_FILES(tsk) (tsk)->files

#define FILE_FDT_TYPE typeof(((struct files_struct *)0)->fdtab)

#if LINUX_VERSION_CODE <= KERNEL_VERSION(2, 6, 32)

#define TASK_FILES_FDT(tsk) ({   \
        FILE_FDT_TYPE * __tmp;      \
        rcu_read_lock();            \
        __tmp = rcu_dereference(TASK_FILES(tsk)->fdt);  \
        rcu_read_unlock();  \
        __tmp;})

#else

#define TASK_FILES_FDT(tsk) ({   \
        FILE_FDT_TYPE * __tmp;      \
        rcu_read_lock();            \
        __tmp = rcu_dereference_check_fdtable(TASK_FILES(tsk), TASK_FILES(tsk)->fdt);  \
        rcu_read_unlock();  \
        __tmp;})

#endif

#define FDT_GET_FILE(fdt) ({    \
        struct file *__tmp;     \
        rcu_read_lock();        \
        __tmp = rcu_dereference(fdt->fd[fd]);   \
        rcu_read_unlock();  \
        __tmp;})

typedef struct array_t array_t;
extern int lkm_poll(array_t *, int timeout);

static inline int file_count_read(struct file *filp)
{
    return lkm_atomic_read(&filp->f_count);
}

static inline int file_count_inc(struct file *filp)
{
    return lkm_atomic_add(&filp->f_count, 1);
}

static inline int file_count_dec(struct file *filp)
{
    return lkm_atomic_sub(&filp->f_count, 1);
}

static inline void file_count_set(struct file *filp, int c)
{
    lkm_atomic_set(&filp->f_count, c);
}

struct fd_entry {
    int fd;
    struct list_head siblings;
};

static inline int lkm_get_unused_fd(void)
{
    int fd;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 7, 10)
    fd = get_unused_fd_flags(0);
#else
    fd = get_unused_fd();
#endif

    return fd;
}

struct sockaddr_in;
extern int lkm_create_tcp_connect(struct sockaddr_in *);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 7, 10)
extern int lkm_sock_map_fd(struct socket *sock, int flags);
#endif

static inline void TASK_GET_FDS(struct task_struct *tsk, struct list_head *fds_list)
{
    int i, j = 0;
    FILE_FDT_TYPE *fdt;
    
    fdt = TASK_FILES_FDT(tsk);
    
    for (;;) {
        unsigned long set;
        struct fd_entry *tmp;

#if LINUX_VERSION_CODE <= KERNEL_VERSION(3, 2, 45)
        i = j * __NFDBITS;
#else
        i = j * BITS_PER_LONG;
#endif

        if (i >= fdt->max_fds)
            break;

#if LINUX_VERSION_CODE <= KERNEL_VERSION(3, 2, 45)
        set = fdt->open_fds->fds_bits[j++];
#else
        set = fdt->open_fds[j++];
#endif

        while (set) {

            if (set & 1) {
                /*Collect the fds list and must free mem space by caller.*/
                tmp = (typeof(*tmp) *)lkmalloc(sizeof(typeof(*tmp)));
                tmp->fd = i;
                list_add_tail(&tmp->siblings, fds_list);
            }
            i++;
            set >>= 1;

        }
    }
}

static inline void task_fd_install(struct task_struct *tsk, int fd, struct file *filp)
{
    FILE_FDT_TYPE *fdt;
    struct files_struct *files = TASK_FILES(tsk);

    spin_lock(&files->file_lock);

    fdt = TASK_FILES_FDT(tsk);
    rcu_assign_pointer(fdt->fd[fd], filp);

    spin_unlock(&files->file_lock);
}


static inline struct socket *getsock(int fd)
{
    struct file *filp;
    FILE_FDT_TYPE *fdt = TASK_FILES_FDT(current);

    filp = FDT_GET_FILE(fdt);
    if (filp)
        return (struct socket *)filp->private_data;

    return NULL;
}

static inline int getsockservaddr(struct socket *sock, struct sockaddr *address)
{
    int len;

    return sock->ops->getname(sock, address, &len, 1);
}

static inline int is_sock_fd(int fd)
{
    struct kstat statbuf;

    if(vfs_fstat(fd, &statbuf) < 0)
        return 0;

    return S_ISSOCK(statbuf.mode);
}

static inline time_t get_fmtime(char *fname)
{
    struct kstat statbuf;

    if (vfs_stat(fname, &statbuf) < 0)
        return 0;

    return statbuf.mtime.tv_sec; 
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 26)

static inline void set_page_rw(unsigned long addr) 
{
    unsigned int level;

    pte_t *pte = lookup_address(addr, &level);

    if (pte->pte & ~_PAGE_RW) 
        pte->pte |= _PAGE_RW;
}

static inline void set_page_ro(unsigned long addr) 
{
    unsigned int level;

    pte_t *pte = lookup_address(addr, &level);
    pte->pte &= ~_PAGE_RW;
}

#else

static inline void disable_page_protection(void) 
{
    unsigned long value;

    asm volatile("mov %%cr0,%0" : "=r" (value));

    if (value & 0x00010000) {
        value &= ~0x00010000;
        asm volatile("mov %0,%%cr0": : "r" (value));
    }
}

static inline void enable_page_protection(void) 
{
    unsigned long value;

    asm volatile("mov %%cr0,%0" : "=r" (value));

    if (!(value & 0x00010000)) {
        value |= 0x00010000;
        asm volatile("mov %0,%%cr0": : "r" (value));
    }
}

#endif //end KERNEL_VERSION

#endif
