#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

#define printfi(level, fmt, ...)            \
    do {                                    \
        for (int i = 0; i < level; i++) {   \
            printf("|\t");                  \
        }                                   \
        printf(fmt, ##__VA_ARGS__);         \
    } while (0)

#define __NR_remap_page_table 451

#define PAGE_SHIFT 12
#define PAGE_SIZE (1UL << PAGE_SHIFT)
#define BIT_MASK ((1UL << 9) - 1)    // 9 bits for indexing (512 entries)
#define PTE_PFN_MASK ((1UL << 48) - 1) // Mask for page frame number 
#define pte_pfn(x) ((x & PTE_PFN_MASK) >> PAGE_SHIFT) // Extract page frame number from PTE

#define LEVEL_PGD 0
#define LEVEL_PUD 1
#define LEVEL_PMD 2
#define LEVEL_PTE 3

struct expose_pgtbl_args {
    pid_t pid;
    unsigned long remap_vaddr;
    unsigned long pfn;
    unsigned int level;
};

void walk_page_table(pid_t pid, unsigned long pfn, unsigned int level) {
    printfi(level, "L%d PGTBL, pfn: %lx\n", level, pfn);

    unsigned long *pgtbl = mmap(NULL, PAGE_SIZE, PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

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

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <pid>\n", argv[0]);
        return 1;
    }
    pid_t pid = atoi(argv[1]);
    if (pid == -1) {
        pid = getpid();
    }
    walk_page_table(pid, 0, LEVEL_PGD);
    return 0;
}