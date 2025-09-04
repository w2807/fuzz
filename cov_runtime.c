#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>

static const char* SHM_ENV_VAR = "__FUZZ_SHARE";
static const size_t COV_MAP_SIZE = 1 << 17;

static uint8_t* cov_area_ptr = NULL;
static __thread uint32_t cov_prev_loc = 0;

__attribute__((constructor))static void __cov_map_shm(void) {
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
    uintptr_t cur = (uintptr_t)guard;
    uintptr_t x = (cur >> 4) ^ (cur << 8);
    uint32_t cur_loc = (uint32_t)(x >> 1);
    uintptr_t idx = (uintptr_t)(cur_loc ^ cov_prev_loc) & (COV_MAP_SIZE - 1);
    cov_area_ptr[idx]++;
    cov_prev_loc = cur_loc >> 1;
}

void __sanitizer_cov_trace_pc_guard_init(uint32_t* start,
                                         const uint32_t* stop) {
    if (start == stop || !start) {
        return;
    }
    for (uint32_t* x = start; x < stop; x++) {
        if (!*x) {
            *x = 1;
        }
    }
}

static void cov_hit(uintptr_t h) {
    if (cov_area_ptr) {
        cov_area_ptr[h & (COV_MAP_SIZE - 1)]++;
    }
}

#define DEF_CMP(N, T) \
void __sanitizer_cov_trace_cmp##N(T a, T b) { \
uintptr_t h = (uintptr_t)a ^ (uintptr_t)b ^ ((uintptr_t)cov_prev_loc << 1) ^ ((uintptr_t)N << 24); \
cov_hit(h); \
} \
void __sanitizer_cov_trace_const_cmp##N(T a, T b) { \
uintptr_t h = (uintptr_t)a ^ (uintptr_t)b ^ ((uintptr_t)cov_prev_loc << 2) ^ ((uintptr_t)N << 25); \
cov_hit(h); \
}

DEF_CMP(1, uint8_t)
DEF_CMP(2, uint16_t)
DEF_CMP(4, uint32_t)
DEF_CMP(8, uint64_t)
