#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <stdbool.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>
#include <getopt.h>

#define GB (1024L * 1024L * 1024L)
#define PAGE_SIZE 4096


size_t total_memory = (size_t)(1.5 * GB);  // per-process malloc 1.5GB
int read_count = 100;    // number of active reads          
int active_ratio = 20;   // active ration (out of 100)                 

volatile sig_atomic_t stop_flag = 0;

void handle_signal(int sig) {
    stop_flag = 1;
}

double current_time_sec() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec + tv.tv_usec / 1e6;
}

// randomly fill page (with random value)
void fill_random(char *page) {
    for (int i = 0; i < PAGE_SIZE; i++) {
        page[i] = rand() % 256;
    }
}

int main(int argc, char *argv[]) {
    int opt;
    srand(time(NULL) + getpid());  // random seed
    
    // get params
    static struct option long_options[] = {
        {"active-ratio", required_argument, 0, 'a'},
        {0, 0, 0, 0}
    };

    while ((opt = getopt_long(argc, argv, "", long_options, NULL)) != -1) {
        switch (opt) {
            case 'a':
                active_ratio = atoi(optarg);
                if (active_ratio < 0 || active_ratio > 100) {
                    fprintf(stderr, "active_ratio 必須在 0-100 之間！\n");
                    exit(1);
                }
                break;
            default:
                fprintf(stderr, "Usage: %s --active-ratio <PERCENT>\n", argv[0]);
                exit(1);
        }
    }

    // get number of pages
    size_t page_count = total_memory / PAGE_SIZE;
    size_t active_page_count = page_count * active_ratio / 100;
    size_t inactive_page_count = page_count - active_page_count;

    printf("[PID=%d] Total pages: %zu | Active: %zu | Inactive: %zu\n",
           getpid(), page_count, active_page_count, inactive_page_count);

    // allocate memory
    char **pages = malloc(page_count * sizeof(char*));
    if (!pages) {
        perror("malloc for page pointers failed");
        exit(1);
    }

    // malloc for all pages
    for (size_t i = 0; i < page_count; i++) {
        pages[i] = malloc(PAGE_SIZE);
        if (!pages[i]) {
            perror("malloc page failed");
            exit(1);
        }
    }

    // Shuffle page order (for random write)
    for (size_t i = page_count - 1; i > 0; i--) {
        size_t j = rand() % (i + 1);
        char *tmp = pages[i];
        pages[i] = pages[j];
        pages[j] = tmp;
    }

    double start_time = current_time_sec();

    // write pages
    for (size_t i = 0; i < page_count; i++) {
        fill_random(pages[i]);
    }

    printf("[PID=%d] Initial write done at %.3f seconds\n", getpid(), current_time_sec() - start_time);

    // inactive: do nothing haha
    // active: read read read... until signal comes

    signal(SIGTERM, handle_signal);  // set signal handler
    signal(SIGINT, handle_signal);
    printf("[PID=%d] I'm alive! Waiting for my inevitable doom...\n", getpid());

    while(!stop_flag) {
        for (size_t i = 0; i < active_page_count; i++) {
            volatile char sink = pages[i][rand() % PAGE_SIZE];
            (void)sink;  // avoid compiler optimization
        }

        usleep((rand() % 400 + 100) * 1000); // 100~500ms
    }

    double end_time = current_time_sec();
    printf("[PID=%d] Finished at %.3f seconds | Duration: %.3f seconds\n",
           getpid(), end_time, end_time - start_time);

    // Free memory
    for (size_t i = 0; i < page_count; i++) {
        free(pages[i]);
    }
    free(pages);

    printf("[PID=%d] Exiting gracefully after signal received!\n", getpid());
    return 0;
}
