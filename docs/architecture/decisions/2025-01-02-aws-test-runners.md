# 2025-01-02: AWS test runners

## Status
Accepted

## Context

We'd like to run some tests on AWS to check the availability of PMC (Performance Monitoring Counters) and Linux resctrl on different instance types. To do this, we'll want an automated way to run tests on different instance types.

As of writing, the main check will be `cpi-count`, which checks the availability of cycles and instructions, and compares the results of `go-perf` and `perf` to sanity-check the results. 

In the future, we'll want to add more tests and similarly run them on different instance types. For example:

- Checking other counters than cycles and instructions (e.g., LLCMisses)
- Checking the availability of `resctrl` in Linux
- Verifying `resctrl` is able to control memory bandwidth and cache allocation

This decision is about individual, relatively simple checks that run on a single instance. Tests that require complex workloads (e.g., DeathStarBench) are out of scope for this decision.

## Options considered - EC2 based

### Common pros and cons

Pros:

- Easy to run multiple instances
- Gives control over the operating system and AMI, if we need that control in the future.
- Few components running on the VM, so this is less noisy and more conducive to benchmarking.

Cons:

- Only works on AWS. Will require adaptation for other clouds.


### AWS EC2 with User Data

This is the strawman: spin up an EC2 instance, install the necessary tools, run the tests, and then tear down the instance. User Data is a way to run commands when the instance is first launched.

Additional pros:

- None

Additional cons:

- There is no good way to get results out of the instance.
- It is hard to check when tests are done.

### AWS EC2 with a GitHub Self-Hosted Runner

This spins up an EC2 instance that runs a GitHub Actions runner. The runner is labeled specifically for the test that spins it up. The Action then runs the test workflow on the runner it just spun up. At the end of the test, the workflow tears down the runner.

Additional pros:

- Integrated well with GitHub Actions: natively extracts results and continues the workflow when the test is done.

Additional cons:


- More complex than EC2 with User Data (but solves that approach's problems).


## Options considered - Kubernetes based

### Common pros and cons

Pros:

- We might be able to reuse this infrastructure for benchmarks with _complex_ Kubernetes workloads.

Cons:

- Complex. Need to set up a Kubernetes cluster and all its tooling.
- Less control over the operating system and AMI.
- Kubernetes has more components running on the Node (e.g., kubelet) that introduce noise, so this approach is less conducive to benchmarking.



### Spin up a Kubernetes cluster and run the tests in a pod

This is the approach the Cilium uses for its [EKS conformance tests.](https://github.com/cilium/cilium/blob/main/.github/workflows/conformance-eks.yaml#L1).

Additional pros:

- Easy to check for completion and extract results (with `kubectl`).

Additional cons:

- More components to set up and tear down (the Kubernetes control plane) which increases the time it takes to run tests and the cost of running tests.
- Need to write the functionality to extract results ourselves.

### Maintain a persistent Kubernetes cluster with `actions-runner-controller`

following GitHub's ["Autoscaling with self-hosted runners"](https://docs.github.com/en/actions/hosting-your-own-runners/managing-self-hosted-runners/autoscaling-with-self-hosted-runners):

- Run a Kubernetes cluster on one of the cloud providers
- Use GitHub Actions to trigger tests
- Tests run self hosted on the Kubernetes cluster. The [actions-runner-controller](https://github.com/actions/actions-runner-controller) seems to be the official controller for this.
- Each test that requires a specific node type will trigger a runner that only runs on that node type

I believe we can add a nodeSelector [in the AutoscalingRunnerSet](https://github.com/actions/actions-runner-controller/blob/96d1bbcf2fa961e7f64fad45ea8903b741cb3e16/charts/gha-runner-scale-set/templates/autoscalingrunnerset.yaml#L112) from the values.yaml when deploying the controller (under [template.spec](https://github.com/actions/actions-runner-controller/blob/96d1bbcf2fa961e7f64fad45ea8903b741cb3e16/charts/gha-runner-scale-set/values.yaml#L189)). So this might require a controller deployment per node type.

Additional pros:

- Very little spin-up and tear-down code, as the controller handles the scaling. This reulsts in simpler Actions, and more reliable cleanup.
- Tests run on GitHub Runners, so they extract results natively.
- We can spin up similar clusters on other clouds, and reuse the exact same Actions to run the tests on other clouds' instance types.

Additional cons:

- Cluster is relatively complex: needs to anticipate all instance types we want to test on, and add controllers for each. This can be implemented with for loops in a helm chart, but still adds complexity.
- Cluster would be persistent, so it has ongoing cost, regardless of whether tests are running or not.
- The cluster would be maintained separately from the tests, so it might be hard to keep them in sync.


### Replicated.com Compatibility Matrix

It is a service that spins up full Kubernetes clusters for testing, and bills by usage.

Additional pros:

- Easy to spin up and tear down clusters.
- Support for AWS, GCP, Azure, OCI, as well as Openshift, RKE2, and k3s.
- Might have credits for open source projects (at least with Openshift)

Additional cons:

- Needs Kubernetes tooling installed (which complicates the Github Action)
- Markup over using the clouds directly (although it is small)
- No spot instance support


## Decision

We'll use the EC2 + GitHub Actions Runner approach, because it is the simplest way that returns results and is easy to check for completion.

## Consequences

### Positive

- Can write the entire test as a GitHub Action.
- The same approach can be used for benchmarking.
- Can use AWS credits to run tests.

### Negative

- We are currently just enabling AWS. To run on other clouds, the setup and cleanup would need to be updated.

### Risks

- Making cleanup bulletproof would require iteration, which could lead to orphaned runners and their associated costs in the interim.
