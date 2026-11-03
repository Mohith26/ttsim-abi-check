# ttsim-abi-check

I wanted to try Tenstorrent's [ttsim](https://github.com/tenstorrent/ttsim) simulator on my Mac (Apple Silicon, macOS 26.4.1), natively rather than in a Linux VM. This repo is a small C program that `dlopen`s a `libttsim.so` and checks a few of the promises in ttsim's [libttsim API and ABI doc](https://github.com/tenstorrent/ttsim/blob/main/docs/libttsim_api.md).

## Building ttsim on macOS

At ttsim commit `26daa9d` (v1.15.1 source drop), `./make.py :build` fails on macOS with Apple clang because `src/sim.cpp` uses `std::max` without including `<algorithm>` (libstdc++ pulls it in transitively, libc++ does not). With that one include added, every Wormhole and Blackhole config builds and links:

```
#include <algorithm>   // added to src/sim.cpp
```

## What the check does

For each `libttsim.so` it:

1. calls `libttsim_init`, then probes every bus/device slot in config space and prints the vendor and device IDs it finds,
2. checks that a non-zero PCI function reads all-ones,
3. sets a non-default rounding mode (`FE_UPWARD`) and raises a sticky `FE_INEXACT` flag, calls `libttsim_clock`, and checks both are still in place afterwards,
4. compares malloc zone `blocks_in_use` before and after `libttsim_clock` (the doc says nothing is allocated after init),
5. compares the process thread count before init and after the clock call (the doc says the library creates no threads),
6. times `libttsim_clock`.

The device is idle (no firmware or kernels loaded), so the clock rate is only a rough number for an empty chip, not a workload benchmark.

## Run

```
make
./abi_check ~/ttsim/src/_out/release_bh/libttsim.so 1000000
```

## Results (M-series Mac, release builds, 1,000,000 clocks)

| config | endpoints found | checks passed | idle clock rate |
|---|---|---|---|
| wh | 1 | 6/6 | 423 M clocks/s |
| bh | 1 | 6/6 | 503 M clocks/s |
| wh_x2 | 1 | 6/6 | 6.8 M clocks/s |
| wh_x8 | 4 | 6/6 | 1.7 M clocks/s |
| wh_x32 | 32 | 6/6 | 0.49 M clocks/s |
| bh_x2 | 2 | 6/6 | 9.2 M clocks/s |
| bh_x4 | 4 | 6/6 | 5.5 M clocks/s |

The endpoint counts match the MMIO chip counts in ttsim's `src/rules.py` (for example `wh_x2` is one MMIO chip plus one Ethernet-tunneled chip, and `wh_x32` puts each chip on its own bus, buses 0x01 and up). Vendor ID is 0x1e52 for all of them, device 0x401e for Wormhole and 0xb140 for Blackhole.

## Limitations

- macOS only for the heap and thread checks (they use `malloc_zone_statistics` and `task_threads`); on Linux they report 0 and pass trivially.
- The heap check only sees the default malloc zones, not raw `mmap`.
- It does not load firmware or run a kernel, so it says nothing about simulation correctness.
