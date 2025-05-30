#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>
#include <getopt.h>
#include <ctype.h>
#include <math.h>

#define MB (1024ULL * 1024ULL)
#define GB (1024ULL * 1024ULL * 1024ULL)
#define PAGE_SIZE 4096



size_t total_memory = (size_t)(1.5 * GB);  // default per-process malloc 1.5GB
int read_count = 100;    // number of active reads          
int active_ratio = 20;   // active ration (out of 100)                 
int loop_num = 1000; // active for loop number
bool inactive_only = false;



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
void fill_random(char *page, unsigned char *fill_values) {
    for (int i = 0; i < PAGE_SIZE; i++) {
        // page[i] = fill_values[i % 256];
        page[i] = ( rand() % 4 ) * 64;
    }
}

static size_t parse_size(const char *s)
{
    char *end;
    double val = strtod(s, &end);          
    if (val <= 0)
        return 0;

    while (isspace((unsigned char)*end)) ++end;

    if (strncasecmp(end, "gb", 2) == 0)
        val *= GB;
    else if (strncasecmp(end, "mb", 2) == 0)
        val *= MB;
    else
        return 0;  

    if (val > (double)SIZE_MAX)
        return 0;

    return (size_t) llround(val);          
}

int main(int argc, char *argv[]) {
    int opt;
    srand(time(NULL) + getpid());  // random seed
    
    // get params
    static struct option long_options[] = {
        {"active-ratio", required_argument, 0, 'a'},
        {"per-process-memory", required_argument, 0, 'm'},
        {"inactive-only", no_argument, 0, 'i'},
        {0, 0, 0, 0}
    };

    while ((opt = getopt_long(argc, argv, "", long_options, NULL)) != -1) {
        switch (opt) {
            case 'a':
                active_ratio = atoi(optarg);
                if (active_ratio < 0 || active_ratio > 100) {
                    fprintf(stderr, "active_ratio should be between 0-100！\n");
                    exit(1);
                }
                break;
            case 'm': {
                size_t tmp = parse_size(optarg);
                if (!tmp) {
                    fprintf(stderr, "WRONG per-process-memory format. Should be e.g. 70MB, or 100GB\n");
                    exit(EXIT_FAILURE);
                }
                total_memory = tmp;
                fprintf(stderr, "Per-process total memory : %zu\n", total_memory);
                break;
            }
            case 'i': {
                inactive_only = true;
                fprintf(stderr, "This process runs in inactive-only mode.\n");
                break;
            }
            default:
                fprintf(stderr, "Usage: %s --active-ratio <PERCENT> --per-process-memory <SIZE>\n", argv[0]);
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
    // register signal handlers
    signal(SIGTERM, handle_signal);  // set signal handler
    signal(SIGINT, handle_signal);


    double start_time = current_time_sec();

    // write pages
    unsigned char fill_values[256];
    for (int i = 0; i < 256; i++) {
        // fill_values[i] = (rand() % 4) * 64; 
        fill_values[i] = (rand() % 256); 
    }
    for (size_t i = 0; i < page_count; i++) {
        fill_random(pages[i], fill_values);
    }

    printf("[PID=%d] Initial write done at %.3f seconds\n", getpid(), current_time_sec() - start_time);

    // inactive: do nothing haha
    // active: read read read... until signal comes

    if (inactive_only){
        loop_num = 500;
    }
    printf("[PID=%d] I'm alive! Starting active portion...\n", getpid());
    for(int i = 0; i < loop_num; i++) {
        for (size_t i = 0; i < active_page_count; i++) {
            // volatile char sink = pages[i][rand() % PAGE_SIZE];
            // (void)sink;  // avoid compiler optimization
            pages[i][rand() % PAGE_SIZE] = (rand() % 4) * 64;
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

    printf("[PID=%d] Exiting gracefully!\n", getpid());
    return 0;
}
