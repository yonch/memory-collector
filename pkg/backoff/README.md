# Backoff Package

A generic exponential backoff implementation for eBPF programs.

## Overview

This package provides a simple and efficient exponential backoff mechanism that can be used in eBPF programs. It is  useful for scenarios where operations might fail frequently and you want to reduce the frequency of retry attempts.

## Usage

### Basic Usage

```c
#include "backoff.h"

// Initialize state
struct backoff_state state;
backoff_init(&state);

// On operation attempt
if (!backoff_in_backoff(&state) || backoff_should_try(&state, get_random_u32())) {
    // Try the operation
    int result = perform_operation();
    
    if (result > 0) {
        // Success - reset backoff
        backoff_update_success(&state);
    } else {
        // Failure - increase backoff
        backoff_update_failure(&state);
    }
}
```

### Backoff Levels

The backoff mechanism works as follows:

1. First failure: 50% probability of trying (1/2)
2. Second failure: 25% probability of trying (1/4)
3. Third failure: 12.5% probability of trying (1/8)
4. And so on, up to 7 levels
5. Maximum backoff: 1/128 probability (after 7 consecutive failures)
