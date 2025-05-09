# Assignment 2 Write-up
# 1. Explianation of Our Implementation

## A. Add new system call to Linux (40%)

The goal is to implement a new system call that maps page table pages.

### (1) `remap_page_table`

We referred to the [official Linux documentation](https://www.kernel.org/doc/html/v4.10/process/adding-syscalls.html) to implement the new syscall.

First, we add the function prototype to `include/linux/syscalls.h`:

```c=74
struct expose_pgtbl_args;
```

```c=1273
asmlinkage long sys_remap_page_table(struct expose_pgtbl_args __user *arg);
```

Then we register it in the syscall table in `include/uapi/asm-generic/unistd.h`：

```c=889
#define __NR_remap_page_table 451
__SYSCALL(__NR_remap_page_table, sys_remap_page_table)

#undef __NR_syscalls
#define __NR_syscalls 452
```

Finally, we add the fallback implementation in `kernel/sys_ni.c`:

```c=483
COND_SYSCALL(remap_page_table);
```

### (2) `remap_page_table`

The purpose of this implementation is to map the page table of the target process specified by `pid` into the current process’s address space at `remap_vaddr`. We assume the given `pfn` is valid and points to a physical page frame containing a page table, so we do not verify its correctness. The `remap_page_table()` function performs the following three steps:

1. Identify the physical page frame to be mapped. If the level is 0, select the level-0 page table frame of the target process. Otherwise, verify that the provided `pfn` is valid.

2. Check if `remap_vaddr` is page-aligned and locate the corresponding `vma` that contains this address.

3. Call `remap_pfn_range` to map the physical page frame to `remap_vaddr`.

Finally, `remap_page_table()` returns an error code indicating the result.

```c
struct expose_pgtbl_args {
	// PID of the target task
	pid_t pid;
	// User-space virtual address to map the page table page
	unsigned long remap_vaddr;
	// Physical frame number from the previous level page table entry
	unsigned long pfn;
	// page table level (0: PGD, 1: PUD, 2: PMD, 3: PTE)
	unsigned int level;
};

/* This syscall exposes the target process's page table page to the
 * current process's address space.
 */
SYSCALL_DEFINE1(remap_page_table, struct expose_pgtbl_args __user *, args)
{	
	struct task_struct *target_task;
	struct mm_struct *mm;
	struct vm_area_struct *vma;
	int err;

	if (args->level > 3 || args->level < 0)
		// level is out of range
		return -EINVAL;

	// Get the target page frame to map
	if (args->level != 0) {
		// In this case, simply map the page frame
		if (!pfn_valid(args->pfn)) {
			// pfn is invalid
			return -EINVAL;
		}
	} else {
		// In this case, we find the page frame of the page table of the target task
		rcu_read_lock();
		target_task = find_task_by_vpid(args->pid);
		if (!target_task) {
			// Target PID does not exist.
			rcu_read_unlock();
			return -EINVAL;
		}
        
                // increase reference count, preventing the target process from released
		get_task_struct(target_task);
		rcu_read_unlock();
    
		// ignore the original pfn, set pfn to the level 0 page table page
		args->pfn = virt_to_phys(target_task->mm->pgd) >> PAGE_SHIFT;
                // decrease reference count
		put_task_struct(target_task);
	}
    
        // check whether remap_vaddr is page-aligned
	if (!IS_ALIGNED(args->remap_vaddr, PAGE_SIZE))
		return -EINVAL;

	get_task_struct(current);
	mm = get_task_mm(current);
	if (!mm)
		return -EINVAL;

	down_write(&mm->mmap_lock);

	// Find the VMA where the remap_vaddr is located
	vma = find_vma(mm, args->remap_vaddr);
	if (!vma) {
		up_write(&mm->mmap_lock);
		mmput(mm);
		put_task_struct(current);
		return -EINVAL;
	}
    
        // remap a physical memory frame specified by args->pfn to the target virtual address
	err = remap_pfn_range(vma, args->remap_vaddr, args->pfn, PAGE_SIZE, vma->vm_page_prot);

	up_write(&mm->mmap_lock);
	mmput(mm);
	put_task_struct(current);

	return err;
}
```


## B. Userfaultfd Program (45%)

The objective is to handle page faults in userspace.

### (1) pt_cache.c

We implemented `pt_cache.c` to manage a cache of page tables. Before directly mmapping a virtual memory region to hold a page table during a page table walk, we first check whether the target page table has already been mmapped, in order to avoid duplicating the remap of the same page table.

We first define a structure `pt_cache_entry` to store necessary metadata for caching.

```c
struct pt_cache_entry {
    unsigned long pfn;
    int level;
    void *user_va;
    struct pt_cache_entry *next;
};
```

The `cache_insert()` function stores cache entries as a linked list.

```c
static struct pt_cache_entry *cache_head = NULL;

void cache_insert(unsigned long pfn, int level, void *va) {
    struct pt_cache_entry *entry = malloc(sizeof(struct pt_cache_entry));
    entry->pfn = pfn;
    entry->level = level;
    entry->user_va = va;
    entry->next = cache_head;
    cache_head = entry;
}
```
The `cache_lookup()` function traverses the linked list to find a matching `pt_cache_entry`.

```c
void *cache_lookup(unsigned long pfn, int level) {
    for (struct pt_cache_entry *cur = cache_head; cur; cur = cur->next) {
        if (cur->pfn == pfn && cur->level == level)
            return cur->user_va;
    }
    return NULL;
}
```

The `cache_cleanup()` function handles final cleanup.

```c
void cache_cleanup(void) {
    struct pt_cache_entry *cur = cache_head;
    while (cur) {
        munmap(cur->user_va, PAGE_SIZE);
        struct pt_cache_entry *tmp = cur;
        cur = cur->next;
        free(tmp);
    }
    cache_head = NULL;
}
```
### (2) userfaultfd.c

This program contains two threads: Thread A triggers memory faults, while Thread B intercepts page fault signals and handles them in userspace. Thread A writes to memory newly allocated via `mmap()` to trigger a page fault, and Thread B uses `poll()` to intercept the signal, requests a physical page from the kernel, and maps it to the faulting virtual address to resolve the page fault in userspace.

#### a. `va_to_pa`

This function takes a virtual address and returns the corresponding physical address.

When handling a page fault, we request a physical page from the kernel and map it to the faulting virtual address. This function walks the page table to locate the physical address that corresponds to a given virtual address.

During the page table walk, we call the previously implemented `remap_page_table()` syscall to map each page table level into the current process's address space so that its contents can be read and traversed to the next level.

```c
unsigned long va_to_pa(unsigned long va) {

    unsigned long shifts[4] = {39, 30, 21, 12};
    unsigned int level = 0;
    unsigned long pfn = 0; // page table entry value
    void *pt_va = NULL; // page table pointer

    // Remap PGD if needed
    pt_va = cache_lookup(pfn, LEVEL_PGD);
    if (!pt_va) {
        pt_va = mmap(NULL, PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
        if (pt_va == MAP_FAILED) {
            perror("mmap failed");
            return 0;
        }
        if (remap_page_table(getpid(), (unsigned long)pt_va, 0, LEVEL_PGD) < 0) {
            perror("remap_page_table PGD failed");
            munmap(pt_va, PAGE_SIZE);
            return 0;
        }
        cache_insert(0, LEVEL_PGD, pt_va);
    }

    // walk page table to find the corresponding pa
    for (level = 0; level < 4; ++level) {
        unsigned long index = (va >> shifts[level]) & BIT_MASK;
        uint64_t *table = (uint64_t *)pt_va;
        uint64_t entry = table[index];

        if (!(entry & VALID_BIT_MASK)) {
            return 0; // entry invalid
        }

        pfn = pte_pfn(entry); // pfn = [47:12] in page table entry

        if (level < LEVEL_PTE) {
            // obtain next level page table
            pt_va = cache_lookup(pfn, level + 1);
            if (!pt_va) {
                pt_va = mmap(NULL, PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
                if (pt_va == MAP_FAILED) {
                    perror("mmap failed");
                    return 0;
                }
                if (remap_page_table(getpid(), (unsigned long)pt_va, pfn, level + 1) < 0) {
                    perror("remap_page_table failed");
                    munmap(pt_va, PAGE_SIZE);
                    return 0;
                }
                cache_insert(pfn, level + 1, pt_va);
            }
        } else {
            // Leaf entry, return physical address
            return entry;
        }
    }
    return 0;
}
```

#### b. `map_fault_va_to_pa`

This function maps a faulting virtual address to a given physical address.

It walks the page table to locate the `PTE` entry corresponding to the faulting virtual address, and writes the provided physical address into that entry. As in `va_to_pa()`, `remap_page_table()` is used to access each level of the page table by mapping it into userspace.

```c
int map_fault_va_to_pa(unsigned long va, unsigned long pa) {
    unsigned long shifts[4] = {39, 30, 21, 12};
    unsigned long level;
    unsigned long pfn = 0;
    void *pt_va = NULL;

    // Remap PGD if needed
    pt_va = cache_lookup(pfn, LEVEL_PGD);
    if (!pt_va) {
        pt_va = mmap(NULL, PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
        if (pt_va == MAP_FAILED) {
            perror("mmap failed");
            return -1;
        }
        if (remap_page_table(getpid(), (unsigned long)pt_va, 0, LEVEL_PGD) < 0) {
            perror("remap_page_table PGD failed");
            munmap(pt_va, PAGE_SIZE);
            return -1;
        }
        cache_insert(0, LEVEL_PGD, pt_va);
    }

    for (level = 0; level < 4; ++level) {
        unsigned long index = (va >> shifts[level]) & BIT_MASK;
        uint64_t *table = (uint64_t *)pt_va;
        uint64_t entry = table[index];

        if (!(entry & VALID_BIT_MASK)) { 
            // not valid
            if (level == LEVEL_PTE) {
                // write PA to entry
                uint64_t new_entry = pa;
                table[index] = new_entry;
                return 0;
            } else {
                // invalid mid-level
                return -1;
            }
        }

        pfn = pte_pfn(entry);

        if (level < LEVEL_PTE) {
            pt_va = cache_lookup(pfn, level + 1);
            if (!pt_va) {
                pt_va = mmap(NULL, PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
                if (pt_va == MAP_FAILED) {
                    perror("mmap failed");
                    return -1;
                }
                if (remap_page_table(getpid(), (unsigned long)pt_va, pfn, level + 1) < 0) {
                    perror("remap_page_table failed");
                    munmap(pt_va, PAGE_SIZE);
                    return -1;
                }
                cache_insert(pfn, level + 1, pt_va);
            }
        } else {
            // already mapped, do not override
            return -1;
        }
    }

    return -1; // should not reach
}
```

#### c. `fault_handler`

The `fault_handler()` function handles page faults.

It uses `poll()` to listen for page fault events. When such an event occurs, it performs the following steps:

1. Retrieve the faulting virtual address.

2. Request a physical memory page from the kernel.

3. Map the physical page to the faulting virtual address.

4. Wake up Thread A after resolving the fault.

Once complete, the function re-enters the poll loop to listen for the next fault.

```c
static void *fault_handler(void *arg) {
    struct handler_args *h = (struct handler_args *)arg; // stores uffd
    struct pollfd pfd = {.fd = h->uffd, .events = POLLIN};
    struct uffd_msg msg;

    for (;;) {
        if (poll(&pfd, 1, -1) <= 0) {
            continue;
        }

        if (read(h->uffd, &msg, sizeof(msg)) != sizeof(msg)) {
            continue;
        }

        if (msg.event != UFFD_EVENT_PAGEFAULT) {
            continue;
        }

        // 1. Get fault address and page-aligned address
        unsigned long page_fault_addr = msg.arg.pagefault.address;
        unsigned long fault_addr = page_fault_addr & ~(PAGE_SIZE - 1); // align to first address of page
s
        void *new_page = mmap(NULL, PAGE_SIZE, PROT_READ | PROT_WRITE,
                            MAP_SHARED | MAP_ANONYMOUS, -1, 0);
        if (new_page == MAP_FAILED) {
            perror("mmap failed");
            continue;
        }
        // 2. access new_page to trigger page fault -> get assigned to PA by kernel
        ((volatile char *)new_page)[0] = 0;

        
        // call va_to_pa() to obtain PA of new_page
        unsigned long pa = va_to_pa((unsigned long)new_page);
        if (pa == 0) {
            fprintf(stderr, "va_to_pa failed: pa is 0\n");
            munmap(new_page, PAGE_SIZE);
            continue;
        }

        // 3. call map_fault_va_to_pa() to update PA to fault_addr's PTE
        if (map_fault_va_to_pa(fault_addr, pa) < 0) {
            fprintf(stderr, "map_fault_va_to_pa failed\n");
            munmap(new_page, PAGE_SIZE);
            continue;
        }


        // 4. Wake up the faulting thread
        struct uffdio_range ur = {
            .start = fault_addr,
            .len = PAGE_SIZE,
        };

        if (ioctl(h->uffd, UFFDIO_WAKE, &ur) == -1) {
            perror("ioctl(UFFDIO_WAKE) failed");
        }
    }

    return NULL;
}
```

## C. Bonus (5%)

Our previous handling caused the same physical page to be mapped twice, which led the kernel to unmap the same physical memory more than once when unmapping memory, resulting in a "BAD PAGE" error. The solution is simple — we just need to reset the remapped page table entry to 0 to resolve the issue.


## D. Compile Userfaultfd Program

We have provided the Makefile, TA can simply run the following command to compile the code.

```sh
make
```

# 2. Compile and Run Test Program

## A. Apply the `syscall.patch` and Build the Kernel

To begin, navigate to the kernel mainline v6.1 directory. Applying the `syscall.patch` to the kernel source code is a prerequisite.

```sh
cd /path_to_kernel
patch -p1 < path/to/syscall.patch
```

Next, the kernel configuration needs to be adjusted with these options:

```sh
make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- defconfig
scripts/config -e CONFIG_USERFAULTFD
scripts/config -d CONFIG_TRANSPARENT_HUGEPAGE
```

After that, the kernel can be built using the following command:

```sh
make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- -j$(nproc)
```

## B. Compile and Run Test Program
```sh
cd test
make
./test_syscall.sh
```
    
## C. Explaination of Test Program

In this test program, we aim to investigate the following:

1.  Whether `remap_page_table()` can successfully map the page table of another process into the virtual memory space of the current process.
2.  Whether the current process can observe kernel modifications to its own page table after it has been mapped using `remap_page_table()`.

The primary testing function in `syscall_test.c` is `walk_page_table()`. This function invokes `remap_page_table()` to traverse all page tables of the target process with process ID `pid` at the specified `level` and all levels below it. These page tables are mapped into the current process's address space to facilitate the first test objective.

Regarding the second test objective, this process allocates a memory region, which indirectly modifies the page table. We then observe whether this modification is reflected in the mapped page table. During the traversal of the lowest-level page table, we first record all entries encountered by performing a bitwise XOR operation on them. Without unmapping the page table, we subsequently use `mmap()` to map a memory region of size `PAGESIZE` and modify its contents. If this allocated memory happens to reside within the current process's page table, we expect to observe a change in the page table entry (verified using the XOR cancellation property). This observation would thus validate the second test objective.

```c
void walk_page_table(pid_t pid, unsigned long pfn, unsigned int level) {
    printfi(level, "L%d PGTBL, pfn: %lx\n", level, pfn);

    unsigned long *pgtbl = mmap(NULL, PAGE_SIZE, PROT_READ | PROT_WRITE,
        MAP_SHARED | MAP_ANONYMOUS, -1, 0);

    struct expose_pgtbl_args args = {
        .pid = pid,
        .remap_vaddr = (unsigned long)pgtbl,
        .pfn = pfn,
        .level = level
    };

    long ret = syscall(__NR_remap_page_table, &args);
    if (ret < 0) {
        perror("syscall failed");
        munmap(pgtbl, PAGE_SIZE);
        return;
    }

    // Walk the page table recursively
    unsigned long page_xor = 0;
    for (int i = 0; i < (PAGE_SIZE / sizeof(unsigned long)); i++) {
        unsigned long entry = pgtbl[i];
        if (entry & BIT_MASK) {
            page_xor ^= entry;
            unsigned long next_pfn = pte_pfn(entry);

            if (level < LEVEL_PTE) {
                // Recursively walk the next level
                walk_page_table(pid, next_pfn, level + 1);
            } else {
                printfi(level + 1, "%d -> PFN: 0x%lx\n", i, next_pfn);
            }
        }
    }

    // Check if can see kernel's modification, "Kao4 Sai4"
    if (pid == getpid() && level == 3) {
        // Allocate a new page and modify it
        unsigned long *new_page = mmap(NULL, PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        for (int i = 0; i < (PAGE_SIZE / sizeof(unsigned long)); i++) {
            new_page[i] = i;
        }

        sleep(1); // Sleep for 1 second to allow kernel to modify the page table

        for (int i = 0; i < (PAGE_SIZE / sizeof(unsigned long)); i++) {
            unsigned long entry = pgtbl[i];
            if (entry & BIT_MASK) {
                page_xor ^= entry;
            }
        }

        if (page_xor != 0) {
            printf("Can see kernel's modification\n"); // grep this message
        }

        munmap(new_page, PAGE_SIZE);
    }

    munmap(pgtbl, PAGE_SIZE);
}
```

# 3. Comtribution

- B11902086 王朝群：Implement syscall, test syscall and generate kernel patch.
- B11902085 顏佐霏：Implement `pt_cache.c` and `userfaultfd.c`.
- B11902124 左峻豪：Complete the write-up document, debug and correct `userfaultfd.c`, implement bonus part.

# 4. Reference

1. [Adding a New System Call — The Linux Kernel documentation](https://www.kernel.org/doc/html/v4.10/process/adding-syscalls.html)