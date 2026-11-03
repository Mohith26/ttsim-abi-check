// Small host harness that dlopens libttsim and checks a few promises from
// ttsim's docs/libttsim_api.md: config-space enumeration, FP environment
// preservation, no new threads, and no heap allocation after init.
#include <dlfcn.h>
#include <fenv.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#if defined(__APPLE__)
#include <mach/mach.h>
#include <malloc/malloc.h>
#endif

typedef void (*fn_void)(void);
typedef uint32_t (*fn_cfg_rd)(uint32_t, uint32_t);
typedef void (*fn_clock)(uint32_t);

static int failures = 0;
#define CHECK(cond, ...) do { if (cond) { printf("  PASS  "); } else { printf("  FAIL  "); failures++; } printf(__VA_ARGS__); printf("\n"); } while (0)

static void *sym(void *h, const char *name) {
    void *p = dlsym(h, name);
    if (!p) { fprintf(stderr, "missing symbol %s\n", name); exit(2); }
    return p;
}

static unsigned thread_count(void) {
#if defined(__APPLE__)
    thread_act_array_t threads; mach_msg_type_number_t n = 0;
    if (task_threads(mach_task_self(), &threads, &n) != KERN_SUCCESS) return 0;
    for (unsigned i = 0; i < n; i++) mach_port_deallocate(mach_task_self(), threads[i]);
    vm_deallocate(mach_task_self(), (vm_address_t)threads, n * sizeof(thread_act_t));
    return n;
#else
    return 0;
#endif
}

static size_t heap_blocks(void) {
#if defined(__APPLE__)
    malloc_statistics_t s; malloc_zone_statistics(NULL, &s); return s.blocks_in_use;
#else
    return 0;
#endif
}

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s path/to/libttsim.so [clocks]\n", argv[0]); return 2; }
    uint32_t clocks = argc > 2 ? (uint32_t)strtoul(argv[2], NULL, 0) : 1000000;
    void *h = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!h) { fprintf(stderr, "dlopen: %s\n", dlerror()); return 2; }
    fn_void init = (fn_void)sym(h, "libttsim_init"), fini = (fn_void)sym(h, "libttsim_exit");
    fn_cfg_rd cfg_rd = (fn_cfg_rd)sym(h, "libttsim_pci_config_rd32");
    fn_clock clk = (fn_clock)sym(h, "libttsim_clock");

    printf("%s\n", argv[1]);
    unsigned threads_before = thread_count();
    init();

    // Enumerate endpoints: probe every bus/device slot, function 0.
    int endpoints = 0;
    for (uint32_t bus = 0; bus < 256; bus++)
        for (uint32_t dev = 0; dev < 32; dev++) {
            uint32_t bdf = (bus << 8) | (dev << 3);
            uint32_t id = cfg_rd(bdf, 0);
            if (id == 0xFFFFFFFFu) continue;
            if (endpoints < 4) printf("  endpoint bus=%02x dev=%02x vendor=0x%04x device=0x%04x\n", bus, dev, id & 0xFFFF, id >> 16);
            endpoints++;
        }
    printf("  %d endpoint(s) found\n", endpoints);
    CHECK(endpoints > 0, "config space exposes at least one endpoint");
    CHECK(cfg_rd(0x0001, 0) == 0xFFFFFFFFu, "non-zero function reads all-ones");

    // FP environment: caller sets a non-default rounding mode and raises a sticky flag;
    // both must be back in place after the call.
    fesetround(FE_UPWARD);
    feclearexcept(FE_ALL_EXCEPT);
    feraiseexcept(FE_INEXACT);
    size_t heap_before = heap_blocks();
    struct timespec t0, t1; clock_gettime(CLOCK_MONOTONIC, &t0);
    clk(clocks);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    size_t heap_after = heap_blocks();
    int round_after = fegetround();
    int inexact_after = fetestexcept(FE_INEXACT);
    fesetround(FE_TONEAREST);
    CHECK(round_after == FE_UPWARD, "caller rounding mode preserved across libttsim_clock");
    CHECK(inexact_after != 0, "caller sticky FP flag preserved across libttsim_clock");
    CHECK(heap_after == heap_before, "no heap blocks allocated or freed by libttsim_clock (%zu -> %zu)", heap_before, heap_after);
    CHECK(thread_count() == threads_before, "no threads created (%u -> %u)", threads_before, thread_count());
    double secs = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;
    printf("  libttsim_clock(%u) took %.3f s (%.2f M clocks/s, idle device)\n", clocks, secs, clocks / secs / 1e6);

    fini();
    printf("%s\n", failures ? "FAILED" : "OK");
    return failures ? 1 : 0;
}
