# 2025-02-18: Collecting Intel CMT Measurements

## Context

We need to collect Intel CMT (Cache Monitoring Technology) measurements at millisecond granularity for containers in a cloud-native environment. 

## Decision

**We will build a kernel module that interacts directly with Intel RDT MSRs (Model Specific Registers) to configure and read CMT measurements.**

## Rationale

We considered three approaches for collecting CMT measurements:

1. **Using Linux perf counters**: After investigating the Intel CMT-CAT repository and relevant Linux kernel code, we found that although the Intel software repository seemed to support perf counters for CMT, the Linux kernel did not actually implement this. Therefore, using perf was not a viable option.

2. **Using the resctrl filesystem interface**: The Linux kernel's resctrl subsystem provides a filesystem-based interface for configuring Intel RDT and reading measurements. However, this approach has several drawbacks:
   - Collecting measurements at millisecond granularity through the filesystem interface for all containers would be complex and potentially inefficient due to the overhead of system calls. 
   - Resctrl is based on tasks and processes rather than containers. To use resctrl, we would need to build a system to monitor container lifecycle events and configure resctrl accordingly, which would add complexity and potential gaps in measurement.

3. **Building a kernel module to interact with MSRs directly**: This approach offers several advantages:
   - By interacting with MSRs directly, the kernel module can read CMT information with very low overhead, without the layers of the filesystem interface.
   - The kernel module can probe container lifecycle tracepoints to allocate RMIDs (Resource Monitoring IDs) and assign them to containers automatically.
   - This approach enables a cloud-native solution that seamlessly measures containers as they are created.

Given these considerations, we chose to build a kernel module that interacts with Intel RDT MSRs directly. This approach provides the best performance, flexibility, and compatibility with a cloud-native container environment.

## Consequences

Building a kernel module for CMT measurement has the following consequences:

- We will need to maintain the kernel module code and ensure compatibility with different Linux kernel versions.
- Users will need to load the kernel module to enable CMT measurement collection.
- We will have tight integration with container lifecycle events, enabling seamless measurement of containers.
- We can achieve low-overhead, millisecond-granularity measurement collection, meeting our performance requirements.

## Status
Accepted


## Appendix A: intel-cmt-cat summary

The `perf_monitoring.c` file:

- Checks if perf is available by checking if `/proc/sys/kernel/perf_event_paranoid` exists.
- Checks if RDT exists by reading `/sys/devices/intel_cqm/type`
    - if it exists, its value (as integer) is the perf `type` field
    - traverses `/sys/devices/intel_cqm/events` for events `llc_occupancy`, `local_bytes`, `total_bytes`
    - their value is parsed to get the `config` field of the perf struct
    - the same file with extension `.scale` is used to read a `double` scale

### Mentions of using the perf command line

Here are references from the web for monitoring RDT using perf. However note that we found that the patches discussed in these references were not present in the Linux kernel whose code we checked (6.13.2) and appear to have not been merged into the kernel originally.

A 2017 [forum post](https://community.intel.com/t5/Software-Tuning-Performance/How-to-use-perf-event-open-function-exposed-by-linux-kernel-to/td-p/1144059) was able to view events with `perf stat` as events:
> `intel_cqm/llc_occupancy , intel_cqm/llc_local_bytes/,intel_cqm_total_bytes/`

(the last value seems to have a typo replacing `/` with `_`)

An [Intel/Kanaka Juvva presentation at LinuxCon'2015](http://events17.linuxfoundation.org/sites/events/files/slides/LinuxConNA-kanaka.pdf) shows per-application memory bandwidth monitoring with `perf` (slide 11):

> Two perf events are exported to userland
> - LOCAL_BW
>   - perf stat –e intel_cqm/llc_local_bw/ -a “my_application”
> - TOTAL_BW
>   - perf stat –e intel_cqm/llc_total_bw/ -a “my_application”

A [2016 Kanaka Juuva presentation](http://events17.linuxfoundation.org/sites/events/files/slides/CollaborationSummit2016-Slides-Kanaka_0.pdf):
- further mentions LLC Occupancy
- shows memory bandwidth benchmark results
- shows more process-based CLI examples, by PID:
> - LLC_OCCUPANCY
>   - perf stat –e intel_cqm/llc_occupancy/ -p “pid of my_application”
- discusses cgroups-based measurements. This might have been before the switch from cgroup to resctrl.

## Appendix B: A journey through intel-cmt-cat

The [intel-cmt-cat](https://github.com/intel/intel-cmt-cat) repo documentation suggests perf can read CMT data as well ([table 5 in README](https://github.com/intel/intel-cmt-cat?tab=readme-ov-file#software-compatibility)).

In this section, we look into how intel-cmt-cat uses perf, and document its usage so we can support that alongside the other counters.

We start with the `pqos` CLI tool. Its command line parameters set up calls into the library in `lib/`:

- [`main`](https://github.com/intel/intel-cmt-cat/blob/667d224cfd689d3b3e63e58b3debc9e672eb87f4/pqos/main.c#L1178) calls [`selfn_monitor_cores`](https://github.com/intel/intel-cmt-cat/blob/667d224cfd689d3b3e63e58b3debc9e672eb87f4/pqos/monitor.c#L1141) on the `-m` command line option.
- [`parse_monitor_cores`](https://github.com/intel/intel-cmt-cat/blob/667d224cfd689d3b3e63e58b3debc9e672eb87f4/pqos/monitor.c#L1100) parses the `-m` command line option.
- [`parse_monitor_group`](https://github.com/intel/intel-cmt-cat/blob/667d224cfd689d3b3e63e58b3debc9e672eb87f4/pqos/monitor.c#L1024) parses a string from the command line to a list of cores or pids, and calls `grp_add` on each.
- [`grp_add`](https://github.com/intel/intel-cmt-cat/blob/667d224cfd689d3b3e63e58b3debc9e672eb87f4/pqos/monitor.c#L815) allocates a `struct mon_group` called `new_grp` on the stack, then adds the core/pid/channel/etc. to the group using `grp_set_*`, and then appends it to a global variable `sel_monitor_group`.
- later, [`main` calls `monitor_setup`](https://github.com/intel/intel-cmt-cat/blob/667d224cfd689d3b3e63e58b3debc9e672eb87f4/pqos/main.c#L1508)
- `monitor_setup` calls the library API depending on the type of monitor. For cores, it [calls `pqos_mon_start_cores`](https://github.com/intel/intel-cmt-cat/blob/667d224cfd689d3b3e63e58b3debc9e672eb87f4/pqos/monitor.c#L1434).

Going into the library:

- [`pqos_mon_start_cores`](https://github.com/intel/intel-cmt-cat/blob/667d224cfd689d3b3e63e58b3debc9e672eb87f4/lib/api.c#L955) calls `pqos_mon_start_cores_ext` (which also has an opt parameter)
- `pqos_mon_start_cores_ext` checks input validity and then makes an [`API_CALL(mon_start_cores...)`](https://github.com/intel/intel-cmt-cat/blob/667d224cfd689d3b3e63e58b3debc9e672eb87f4/lib/api.c#L1010)
- [`API_CALL`](https://github.com/intel/intel-cmt-cat/blob/667d224cfd689d3b3e63e58b3debc9e672eb87f4/lib/api.c#L265) is a macro that accesses a virtual table of monitoring operations called `api` in `api.c`. 
    - This `api` variable is initialized in [`api_init`](https://github.com/intel/intel-cmt-cat/blob/667d224cfd689d3b3e63e58b3debc9e672eb87f4/lib/api.c#L184) to either the OS interface or MSR interface (these are mentioned in the repo's README).
    - In the OS interface, the `mon_start_cores` function pointer is [initialized](https://github.com/intel/intel-cmt-cat/blob/667d224cfd689d3b3e63e58b3debc9e672eb87f4/lib/api.c#L227) to point to `os_mon_start_cores`.
- [`os_mon_start_cores`](https://github.com/intel/intel-cmt-cat/blob/667d224cfd689d3b3e63e58b3debc9e672eb87f4/lib/os_monitoring.c#L378) validates the input, the available monitoring capabilities, and ensures the monitoring hadn't already started, and calls `os_mon_start_events`.
- [`os_mon_start_events`](https://github.com/intel/intel-cmt-cat/blob/667d224cfd689d3b3e63e58b3debc9e672eb87f4/lib/os_monitoring.c#L166):
    - runs `perf_mon_is_event_supported` on every event, and if so, calls `perf_mon_start`.
    - otherwise, checks `resctrl_mon_is_event_supported` and if so performs `resctrl_mon_start`.

Let's explore the flow that checks perf for supported events:

- [`perf_mon_is_event_supported`](https://github.com/intel/intel-cmt-cat/blob/667d224cfd689d3b3e63e58b3debc9e672eb87f4/lib/perf_monitoring.c#L702) calls `get_supported_event`.
- [`get_supported_event`](https://github.com/intel/intel-cmt-cat/blob/667d224cfd689d3b3e63e58b3debc9e672eb87f4/lib/perf_monitoring.c#L159) looks up the event in a global `events_tab`.
    - the first event in `events_tab` is [`llc_occupancy`](https://github.com/intel/intel-cmt-cat/blob/667d224cfd689d3b3e63e58b3debc9e672eb87f4/lib/perf_monitoring.c#L90).

Initialization of perf monitoring in [`perf_mon_init`](https://github.com/intel/intel-cmt-cat/blob/667d224cfd689d3b3e63e58b3debc9e672eb87f4/lib/perf_monitoring.c#L415):

- if `/proc/sys/kernel/perf_event_paranoid` exists, enables the PMU events (cycles, instructions, IPC, LLC misses, LLC references).
- [`set_arch_event_attrs`](https://github.com/intel/intel-cmt-cat/blob/667d224cfd689d3b3e63e58b3debc9e672eb87f4/lib/perf_monitoring.c#L227) sets the `attr` field on PMU events. The `attr` field is a [`struct perf_event_attr`](https://github.com/torvalds/linux/blob/21266b8df5224c4f677acf9f353eecc9094731f0/include/uapi/linux/perf_event.h#L389) (from the linux API).
- [`set_mon_type`](https://github.com/intel/intel-cmt-cat/blob/667d224cfd689d3b3e63e58b3debc9e672eb87f4/lib/perf_monitoring.c#L193) reads `/sys/devices/intel_cqm/type` as an integer into the global variable `os_mon_type`. This int is then used in the perf attr as its `type` field in [`set_rdt_event_attrs`](https://github.com/intel/intel-cmt-cat/blob/667d224cfd689d3b3e63e58b3debc9e672eb87f4/lib/perf_monitoring.c#L334).
- [`set_mon_events`](https://github.com/intel/intel-cmt-cat/blob/667d224cfd689d3b3e63e58b3debc9e672eb87f4/lib/perf_monitoring.c#L350) then traverses the directory `/sys/devices/intel_cqm/events`. 
    - For each file, it tries to find an entry in `events_tab` whose `name` field is the same as the file name. 
    - For every match, it calls `set_rdt_event_attrs`.
- [`set_rdt_event_attrs`](https://github.com/intel/intel-cmt-cat/blob/3bae201f1900e529cff09c62aa7bc21b5ccaac75/lib/perf_monitoring.c#L281)
    - reads the file
    - assumes the contents has a `=`, discards everything before the first `=` and parses the rest as an integer. this will be `attrs.config`
    - reads another file filename+`.scale` suffix
    - parses it as a double. this will be the event's `scale`

So far, we covered initialization and checking event availability. Now let's see how the library configures the kernel to start monitoring:


- [`perf_event_open`](https://github.com/intel/intel-cmt-cat/blob/b9fd9f595f4cc1c404ba24f18da85951e4e9d922/lib/perf.c#L56) makes the syscall via `syscall(__NR_perf_event_open, attr, pid, cpu, group_fd, flags)`


