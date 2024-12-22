# 2024-12-21: Notifications for container lifecycle events

## Status
Draft, WIP

## Context
We'd like the collector to show how memory resource contention influences container performance.

To do that, we'd need to monitor:
1. Resource contention - can do this with `resctrl`, or by monitoring LLC Misses using perf counters
2. Container performance - current plan is to do this by monitoring CPI (cycles per instruction)

For CPI monitoring, we'd need to have an inventory of containers on the system, and correctly instrument them as they arrive/go. In this issue, we add a component to monitor the arrival and departure of containers in the system.

## Options considered

### Kubelet API
If we're focusing on Kubernetes, kubelet provides an HTTP API accessible locally. This appears to be an undocumented, unstable API, that is nevertheless available in kubelet.

[Stack overflow discussion](https://stackoverflow.com/questions/35075195/is-there-api-documentation-for-kubelet-api) points to a project [kubeletctl](https://github.com/cyberark/kubeletctl). The referenced [blog post](https://www.cyberark.com/resources/threat-research-blog/using-kubelet-client-to-attack-the-kubernetes-cluster) shows several `curl` commands to interact with the API. According to the blog post, this is available because the default kubelet configuration allows for anonymous (unauthenticated) requests, so this relies on users not fortifying their systems to this vulnerability. The specific implementation in kubeletctl appears a thin implementation of HTTP calls, so it might be best to reimplement this in our on library rather than take a dependency.

Pros:
- Should provide metadata on Pods, not only containers
- Does not rely on a specific container runtime (docker, containerd, etc.)

Cons:
- Undocumented, unstable API
- Requires access to kubelet, which may not be available in all environments
- Appears to require polling (no `watch`). If so, will react slowly and incur more overhead.

### Filesystem watch on the cgroup directory (e.g., `inotify`)

This is the method used by [Koordinator.sh](https://github.com/koordinator-sh/koordinator) in its [PLEG component](https://github.com/koordinator-sh/koordinator/blob/a62dd49f0fbe84a9298cf6df81c0c895b78cbd6a/pkg/koordlet/pleg/pleg.go#L138). It watches the cgroup root path for each of the Kubernetes QoS classes, for new pod directories. A new pod directory adds that pod subdirectory to a container watcher, which then issues container events.

Pros:
- Does not require access to kubelet
- Does not depend on a container runtime
- ABI is stable and well-documented
- Supports inotify, which is efficient and low-overhead

Cons:
- Does not provide metadata beyond the pod and container IDs

### CRI (Container Runtime Interface) events

### Kubernetes API (i.e., watching the control plane)

## Decision