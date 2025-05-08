#define _GNU_SOURCE

#include <fcntl.h>
#include <linux/userfaultfd.h>
#include <poll.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include <unistd.h>        // for syscall()
#include <sys/syscall.h>   // for __NR_remap_page_table
#include <stdint.h>        // for uint64_t / uintptr_t types
#include <errno.h>         // for errno
#include <string.h>        // for strerror()
#include <stdio.h>         // for perror / fprintf
#include <stdlib.h>        // for exit / malloc
#include <sys/mman.h>      // for mmap()
#include <sys/types.h>     // for pid_t
#include "pt_cache.h"
#include <limits.h>
#define PAGE_SHIFT 12
#define PAGE_SIZE (1UL << PAGE_SHIFT)
#define BIT_MASK ((1UL << 9) - 1)    // 9 bits for indexing (512 entries)
#define PFN_MASK ((1UL << 47) - 1)  
#define VALID_BIT_MASK 0x1
// #define PTE_FLAGS (0x705UL)  // Valid + AF + SH bits

#define PTE_VALID     (1UL << 0)
#define PTE_TYPE_PAGE (1UL << 1)      // entry is a page, not a table
#define PTE_AF        (1UL << 10)     // accessed
#define PTE_SH_INNER  (3UL << 8)      // inner-shareable
#define PTE_AP_RW     (0UL << 6)      // EL0 read/write
#define PTE_ATTR_IDX  (0UL << 2)      // normal memory
#define PTE_UXN       (1UL << 54)
#define PTE_PXN       (1UL << 53)

#define PTE_FLAGS (PTE_VALID | PTE_TYPE_PAGE | PTE_AF | PTE_SH_INNER | PTE_AP_RW | PTE_ATTR_IDX | PTE_UXN | PTE_PXN)


// Page table levels
#define LEVEL_PGD 0
#define LEVEL_PUD 1
#define LEVEL_PMD 2
#define LEVEL_PTE 3
#define __NR_remap_page_table 451

// Arguments for exposing page tables
struct expose_pgtbl_args {
    pid_t pid; // process 
    unsigned long remap_vaddr; // remap virtual address
    unsigned long pfn; // page frame number
    unsigned int level; // page table level
};

// Arguments for the fault handler thread
struct handler_args {
    int uffd;
};

// track new_page mmap
struct page_node {
    void *addr;
    struct page_node *next;
};

static struct page_node *page_list = NULL;

void track_new_page(void *addr) {
    struct page_node *node = malloc(sizeof(struct page_node));
    node->addr = addr;
    node->next = page_list;
    page_list = node;
}

void free_new_pages(void) {
    struct page_node *cur = page_list;
    while (cur) {
        munmap(cur->addr, PAGE_SIZE);
        struct page_node *tmp = cur;
        cur = cur->next;
        free(tmp);
    }
    page_list = NULL;
}


// wrapper function for calling remap_page_table() syscall
static inline int remap_page_table(pid_t pid, unsigned long remap_vaddr, unsigned long pfn, unsigned int level) {
    struct expose_pgtbl_args args = {
        .pid = pid,
        .remap_vaddr = remap_vaddr,
        .pfn = pfn,
        .level = level
    };
    return syscall(__NR_remap_page_table, &args);
}


unsigned long va_to_pa(unsigned long va) {

    unsigned long shifts[4] = {39, 30, 21, 12};
    unsigned long level = 0;
    unsigned long pfn = 0; // page table entry value
    unsigned long *pt_va = NULL; // page table pointer

    // Step 1: Remap PGD if needed
    pt_va = cache_lookup(pfn, LEVEL_PGD);
    if (!pt_va) {
        pt_va = mmap(NULL, PAGE_SIZE, PROT_READ, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
        if (pt_va == MAP_FAILED) {
            perror("mmap failed");
            return 0;
        }
        int ret = remap_page_table(getpid(), (unsigned long)pt_va, 0, LEVEL_PGD);
        if (ret < 0) {
            // perror("remap_page_table PGD failed");
            fprintf(stderr, "va_to_pa level PGD -> Error code : %d\n", -ret);
            // munmap(pt_va, PAGE_SIZE);
            return 0;
        }
        cache_insert(0, LEVEL_PGD, pt_va, va);
    }

    for (level = 0; level < 4; ++level) {
        unsigned long index = (va >> shifts[level]) & BIT_MASK;
        uint64_t *table = (uint64_t *)pt_va;
        uint64_t entry = table[index];

        if (!(entry & VALID_BIT_MASK)) {
            return 0; // entry invalid
        }

        pfn = ((entry & PFN_MASK) >> PAGE_SHIFT); // pfn = [47:12] in page table entry

        if (level < LEVEL_PTE) {
            // obtain next level page table
            pt_va = cache_lookup(pfn, level + 1);
            if (!pt_va) {
                pt_va = mmap(NULL, PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
                if (pt_va == MAP_FAILED) {
                    perror("mmap failed");
                    return 0;
                }
                int ret = remap_page_table(getpid(), (unsigned long)pt_va, pfn, level + 1);
                if ( ret < 0) {
                    // perror("remap_page_table failed");
                    fprintf(stderr, "va_to_pa level %ld -> Error code : %d\n", level, -ret);
                    // munmap(pt_va, PAGE_SIZE);
                    return 0;
                }
                cache_insert(pfn, level + 1, pt_va, va);
            }
        } else {
            // Leaf entry, return physical address
            return entry;
	        //return (pfn << PAGE_SHIFT) | (va & (PAGE_SIZE - 1));
        }
    }
    return 0;
}


void print_binary(unsigned long x) {
    int bits = sizeof(x) * CHAR_BIT;
    for (int i = bits - 1; i >= 0; i--) {
        putchar( (x & (1UL << i)) ? '1' : '0' );
        if (i % 8 == 0 && i!=0) putchar(' '); // 每 8 位加个空格，方便阅读
    }
    putchar('\n');
}

int map_fault_va_to_pa(unsigned long va, unsigned long pa) {
    unsigned long shifts[4] = {39, 30, 21, 12};
    unsigned long level;
    unsigned long pfn = 0;
    void *pt_va = NULL;

    // Step 1: Remap PGD if needed
    pt_va = cache_lookup(pfn, LEVEL_PGD);
    if (!pt_va) {
        pt_va = mmap(NULL, PAGE_SIZE, PROT_READ, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
        if (pt_va == MAP_FAILED) {
            perror("mmap failed");
            return -1;
        }
        int ret = remap_page_table(getpid(), (unsigned long)pt_va, 0, LEVEL_PGD);
        if (ret < 0) {
            // perror("remap_page_table PGD failed");
            fprintf(stderr, "map level PGD -> Error code : %d\n", -ret);
            // munmap(pt_va, PAGE_SIZE);
            return -1;
        }
        cache_insert(0, LEVEL_PGD, pt_va, va);
    }

    for (level = 0; level < 4; ++level) {
        unsigned long index = (va >> shifts[level]) & BIT_MASK;
        uint64_t *table = (uint64_t *)pt_va;
        uint64_t entry = table[index];

        if (!(entry & VALID_BIT_MASK)) { 
            // not valid
            if (level == LEVEL_PTE) {
                // write PA to entry
                print_binary(pa);
		print_binary((pa & ~(PAGE_SIZE-1)) | PTE_FLAGS );
		uint64_t new_entry = pa; //(pa & ~(PAGE_SIZE - 1)) | PTE_FLAGS; 
                table[index] = new_entry;
		print_binary(table[index]);
                if (table[index] != new_entry) {
                    fprintf(stderr, "write to PTE failed silently...\n");
                }
                return 0;
            } else {
                // invalid mid-level
                return -1;
            }
        }

        
        pfn = (entry & PFN_MASK) >> PAGE_SHIFT;

        if (level < LEVEL_PTE) {
            // get next level page table
            pt_va = cache_lookup(pfn, level + 1);
            if (!pt_va) {
                pt_va = mmap(NULL, PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
                if (pt_va == MAP_FAILED) {
                    perror("mmap failed");
                    return -1;
                }
                int ret = remap_page_table(getpid(), (unsigned long)pt_va, pfn, level + 1);
                if (ret < 0) {
                    // perror("remap_page_table failed");
                    fprintf(stderr, "map level %ld -> Error code : %d\n", level, -ret);
                    // munmap(pt_va, PAGE_SIZE);
                    return -1;
                }
                cache_insert(pfn, level + 1, pt_va, va);
            }
        } else {
            // already mapped, do not override
            fprintf(stderr, "WHY! help me : faulting on existing mapping??\n");
            return -1;
        }
    }

    return -1; // should not reach
}


// Page fault handler thread
static void *fault_handler(void *arg) {
    struct handler_args *h = (struct handler_args *)arg; // stores uffd
    struct pollfd pfd = {.fd = h->uffd, .events = POLLIN};
    struct uffd_msg msg;

    for (;;) {
        fprintf(stderr, "[+]NEW poll : \n");
        if (poll(&pfd, 1, -1) <= 0) {
            continue;
        }

        if (read(h->uffd, &msg, sizeof(msg)) != sizeof(msg)) {
            continue;
        }

        if (msg.event != UFFD_EVENT_PAGEFAULT) {
            continue;
        }

        // Get fault address and page-aligned address
        unsigned long page_fault_addr = msg.arg.pagefault.address;
        unsigned long fault_addr = page_fault_addr & ~(PAGE_SIZE - 1); // align to first address of page
        fprintf(stderr, "@ fault_va = %#lx\n", fault_addr);
        // TODO : provide a physical address for fault_addr + add entry to PTE
        void *new_page = mmap(NULL, PAGE_SIZE, PROT_READ | PROT_WRITE,
                            MAP_SHARED | MAP_ANONYMOUS, -1, 0);
        track_new_page(new_page);
        if (new_page == MAP_FAILED) {
            perror("mmap failed");
            continue;
        }
        // access new_page to trigger page fault -> get assigned to PA by kernel
        ((volatile char *)new_page)[0] = 0;

        // call va_to_pa() to obtain PA of new_page
        fprintf(stderr, "va_to_pa...\n");
        unsigned long pa = va_to_pa((unsigned long)new_page);
        if (pa == 0) {
            fprintf(stderr, "va_to_pa failed: pa is 0\n");
            // munmap(new_page, PAGE_SIZE);
            continue;
        }

        // call map_fault_va_to_pa() to update PA to fault_addr's PTE
        fprintf(stderr, "map_fault_va_to_pa ...\n");
        if (map_fault_va_to_pa(fault_addr, pa) < 0) {
            fprintf(stderr, "map_fault_va_to_pa failed\n");
            // munmap(new_page, PAGE_SIZE);
            continue;
        }

        // unsigned long check_pa = va_to_pa(fault_addr);
        // fprintf(stderr, "→ after map: va 0x%lx → pa 0x%lx (expected 0x%lx)\n", fault_addr, check_pa, pa);


        fprintf(stderr, "UFFDIO_WAKE...\n");
        // Wake up the faulting thread
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

int main(int argc, char **argv) {
    int uffd = syscall(__NR_userfaultfd, O_CLOEXEC | O_NONBLOCK);
    if (uffd < 0) {
        perror("userfaultfd failed");
        return 1;
    }

    struct uffdio_api ua = {.api = UFFD_API};
    if (ioctl(uffd, UFFDIO_API, &ua) == -1) {
        perror("ioctl(UFFDIO_API) failed");
        return 1;
    }

    // Allocate memory region to be handled by userfaultfd
    char *region = mmap(NULL, 10 * PAGE_SIZE, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (region == MAP_FAILED) {
        perror("mmap failed for region");
        return 1;
    }

    // Register the region with userfaultfd
    struct uffdio_register ur = {
        .range.start = (unsigned long)region,
        .range.len = 10 * PAGE_SIZE,
        .mode = UFFDIO_REGISTER_MODE_MISSING,
    };

    if (ioctl(uffd, UFFDIO_REGISTER, &ur) == -1) {
        perror("ioctl(UFFDIO_REGISTER) failed");
        return 1;
    }

    struct handler_args h = {
        .uffd = uffd,
    };

    pthread_t tid;
    if (pthread_create(&tid, NULL, fault_handler, &h) != 0) {
        perror("Failed to create fault handler thread");
        return 1;
    }

    printf("Fault handler thread created\n");

    // Access pages to trigger faults
    for (size_t i = 0; i < 10 * PAGE_SIZE; i += PAGE_SIZE) {
        volatile char c = region[i]; // Read to trigger fault
        printf("Page %zu: First byte was %c\n", i / PAGE_SIZE, c);

        region[i] = 'A' + (i / PAGE_SIZE); // Write to page
        printf("Page %zu: First byte now %c\n", i / PAGE_SIZE, region[i]);
    }

    printf("All pages accessed successfully\n");

    printf("Cleaning up resources...\n");

    pthread_cancel(tid);
    pthread_join(tid, NULL);

    if (ioctl(uffd, UFFDIO_UNREGISTER, &ur) == -1) {
        perror("ioctl(UFFDIO_UNREGISTER) failed");
    }

    close(uffd);
    // // munmap(region, 10 * PAGE_SIZE);

    // TODO : remapped page table cache release
    cache_cleanup(); // unmap cached mappings 
    free_new_pages();   // unmap new_page allocations

    printf("Program completed successfully\n");
    return 0;
}

