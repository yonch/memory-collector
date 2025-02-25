# Synthetic Noise Generators

Synthetic noise generators are programs that cause noisy-neighbor stress on shared resources. In our case, this would be memory bandwidth and cache.

Noise generators can be used for:
1. Testing that metrics the collector outputs are correct
2. When used in a cluster running a multi-service workload like [microservices-demo](https://github.com/GoogleCloudPlatform/microservices-demo), shows how the services behave under noisy neighbor.
3. Can potentially be used in staging environments to get an indication how a company's real workload would behave under noisy neighbor.

## Conclusion

Intel MLC is the best fit, given its wide configurability via the command line, and extensive documentation. The shortcoming is that it is not open source. Installing it requires accepting the license, which might be hard automatically from a CI system, so might require some investment in automation.

## Top contenders

### [Intel Memory Latency Checker (Intel MLC)](https://www.intel.com/content/www/us/en/developer/articles/tool/intelr-memory-latency-checker.html)

Binary distribution (does not seem to be OSS). Is able to gather multiple baselines:
- Idle latency and maximum bandwidth, for each source and destination NUMA node
- Peak memory bandwidth at different read-write ratios
- Latencies under different bandwidth loads

The binary also disables prefetches for the duration of the run for more accurate results.

- It is possible to select the delay between memory accesses in the bandwidth generators (`-d` parameter)
- Can specify array sizes for bandwidth generation (`-b`)
- A latency-measurement thread generates dependent accesses, it might be reusable to measure a histogram of latencies on a live system (without generating bandwidth noise), would need further investigation. The idle_latency mode might do exactly that.
- Control what CPUs allocate memory, and where latency measurements run. (`-i`, `-j`)
- Can specify which CPUs are used for bandwidth measurement (`-k`)
- Ensures entire CPUs are not put in lower frequency because all cores are idle by 100% utilizing a core on each CPU (`-p`)
- In AVX512, can request explicit flushing of cache lines to DRAM (`-P`, `-Q`)
- There is an L3 bandwidth measurement mode that tests just the bandwidth to read from L3 (`-u`)
- Controlling random vs sequential access, both in latency-measurement threads and bandwidth-generation threads
- Read to write ratio (`-W`, `-R`)

### [pmbw](https://panthema.net/2013/pmbw/) ([GitHub](https://github.com/bingmann/pmbw?tab=readme-ov-file))

A C/C++ noise generator for cache and RAM, with the access loops coded in assembly. Tests have several configuration options, to achieve different stress patterns:
- Sequential scanning versus walking permutations
- Read or Write
- Number of bits transferred per instruction, from 16 up to 256 via SSE/MMX/AVX
- Accessing components using pointers versus index-accessing arrays
- Regular tests, or tests with unrolled loop (I assume, to stress the instruction cache or to avoid branches on every iteration)

Appears relatively easy to compile, since it only requires the `pthreads` and `rt` libraries.


## Also considered

### [Sysbench](https://github.com/akopytov/sysbench)

Mentions it is mostly used for database benchmarks, but its README mentions `memory`, a memory access benchmark. The README also documents general purpose command line parameters like the number of threads and warmup time, but a quick scan did not generate more documentation for the `memory` benchmarks.

We found it less likely to be a fit, given its database focus and lack of documentation for the memory benchmark.

### [STREAM](http://www.cs.virginia.edu/stream/)

Mature package, earliest submissions from 1991 (latest update to website benchmark 2017). Seems to be a single C and single FORTRAN source file, with a Makefile. There is very little control over the measured pattern and results are very concise: main tuning point is the size of array being measured `STREAM_ARRAY_SIZE`, and results are printed with:
```
    printf("Function    Best Rate MB/s  Avg time     Min time     Max time\n");
```

A [recent Intel mention](https://www.intel.com/content/www/us/en/developer/articles/technical/optimizing-memory-bandwidth-on-stream-triad.html) shows how to compile an optimized version of STREAM.

STREAM was used in the [Themis](https://dl.acm.org/doi/10.1145/3545008.3545064) paper.

We found it less likely to be a fit, given its lack of control over the stress pattern and results.

### [mmatyas/bandwidth-benchmark](https://github.com/mmatyas/bandwidth-benchmark)

This is a memory and network bandwidth benchmark. The source code indicates development 2005-2016, with many versions, but there is little activity on GitHub. Supports SSE/AVX and random access. Earlier tests run 5 second tests for a total of 35 minutes. CSV output.

This could be a fit, but has less documentation than the alternatives.

### [Cachebench](https://github.com/elijah/cachebench/tree/master)

While used in [Alita](https://ieeexplore.ieee.org/document/9355282/) as LLC polluter, the program seems to have been developed in 1998 at University of Tennessee at Knoxville and the repo has not received additional contributions since. It has a README and pdf guide.

### [iBench](https://github.com/stanford-mast/iBench)

Appears to have been used in multiple papers [Paragon](http://csl.stanford.edu/~christos/publications/2013.paragon.asplos.pdf), [Seer](https://dl.acm.org/doi/pdf/10.1145/3297858.3304004), [Quasar](https://www.csl.cornell.edu/~delimitrou/papers/2014.asplos.quasar.pdf), [FIRM](https://www.usenix.org/system/files/osdi20-qiu.pdf) (alongside `pmbw`), and [PARTIES](https://dl.acm.org/doi/pdf/10.1145/3297858.3304005). It is described in a [paper](http://csl.stanford.edu/~christos/publications/2013.ibench.iiswc.pdf).

The repo appears to contain just 7 of the 15 stressors described in the iBench paper. Its memory bandwidth stressor seems to be less extensive than the memory access functions in `pmbw` (e.g., 860 lines of code in [funcs_x86_64.g](https://github.com/bingmann/pmbw/blob/master/funcs_x86_64.h) vs. 87 lines in [memBw.c](https://github.com/stanford-mast/iBench/blob/master/src/memBw.c)). The memory bandwidth benchmark only receives the length of the benchmark as a parameter, and it is unclear how to adjust the stress intensity.