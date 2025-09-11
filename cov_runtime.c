#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>

static const char* SHM_ENV_VAR = "__FUZZ_SHARE";
static const size_t COV_MAP_SIZE = 1 << 17;

static uint8_t* cov_area_ptr = NULL;
static __thread uint32_t cov_prev_loc = 0;
static uint32_t unique_guard_id = 1;

__attribute__((constructor)) static void __cov_map_shm(void) {
    const char* shm_name = getenv(SHM_ENV_VAR);
    if (!shm_name || !*shm_name) {
        return;
    }
    int fd = shm_open(shm_name, O_RDWR, 0600);
    if (fd < 0) {
        return;
    }
    void* map = mmap(NULL, COV_MAP_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd,
                     0);
    close(fd);
    if (map == MAP_FAILED) {
        return;
    }
    cov_area_ptr = (uint8_t*)map;
}

void __sanitizer_cov_trace_pc_guard(const uint32_t* guard) {
    if (!cov_area_ptr || !guard || !*guard) {
        return;
    }

    uint32_t guard_id = *guard;
    uintptr_t edge_index = (uintptr_t)cov_prev_loc ^ (uintptr_t)guard_id;
    uintptr_t idx = edge_index & (COV_MAP_SIZE - 1);
    cov_area_ptr[idx]++;
    cov_prev_loc = guard_id >> 1;
}

void __sanitizer_cov_trace_pc_guard_init(uint32_t* start,
                                         const uint32_t* stop) {
    if (start == stop || !start) {
        return;
    }
    for (uint32_t* x = start; x < stop; x++) {
        if (!*x) {
            *x = unique_guard_id++;
        }
    }
}
