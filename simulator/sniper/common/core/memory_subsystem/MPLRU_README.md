# MPLRU: Metadata-Priority LRU Cache Replacement Policy

## Overview

MPLRU (Metadata-Priority LRU) is an adaptive cache replacement policy designed for unified NUCA caches that contain both data and page table (metadata) blocks. The key insight is that **page table accesses are on the critical path of address translation**, and protecting them in the cache can significantly reduce translation stalls—but only when translation is actually a bottleneck.

## The Problem

In systems with unified last-level caches (LLC/NUCA), page table entries compete with data for cache space. When memory pressure is high:

1. **Page table thrashing**: Frequently accessed PTEs get evicted by data, causing repeated page walks
2. **Translation stalls dominate**: For workloads with large working sets, translation can consume 10-30% of execution time
3. **Static policies fail**: Always protecting metadata hurts data-intensive workloads; never protecting hurts translation-heavy ones

## The Solution: Adaptive Metadata Priority

MPLRU provides two adaptive controller types:

1. **Delta Controller** (`type = delta`): Uses domain knowledge (translation stalls, data pain) to make decisions
2. **Bandit Controller** (`type = bandit`): Uses IPC-only multi-armed bandit learning

### 6-Level Protection Policy

Both controllers use a 6-level protection scheme:

| Level | Mode | Description |
|-------|------|-------------|
| L0 | OFF | Vanilla LRU, no metadata protection |
| L1 | Gentle Bias | Evict data if in bottom 25% of LRU stack |
| L2 | Medium Bias | Evict data if in bottom 50% of LRU stack |
| L3 | Hard Bias | Always prefer evicting data over metadata |
| L4 | Partition 25% | Reserve 25% of cache ways for metadata |
| L5 | Partition 50% | Reserve 50% of cache ways for metadata |

---

## Controller Type 1: Delta Controller (`type = delta`)

The delta controller uses a **Δ-based push-then-backoff** approach:

```
tr_share = S_tr_ema / C_ema      // Translation stalls as fraction of cycles
rho = S_tr_ema / S_data_ema      // Translation vs data stall ratio

ENGAGE if: (tr_share >= 1%) OR (rho >= 2%)
```

**Hysteresis prevents thrashing:**
- Engage after 2 consecutive epochs above threshold
- Disengage after 4 consecutive epochs below threshold

### 2. Exploration Phase

When first engaged, the controller **explores aggressively**:

```
for first N epochs after engage:
    if data_pain > 2× threshold:
        back off immediately (severe pain)
    else:
        push meta_level higher
```

This ensures we "try prioritizing translation" before evaluating effectiveness.

### 3. Δ-Controller (Post-Exploration)

After exploration, the controller uses **marginal-effect control**:

```
Δ_tr = S_tr_ema - prev_S_tr_ema     // Change in translation stalls
Δ_data = S_data_ema - prev_S_data_ema  // Change in data stalls

g_tr = -Δ_tr / S_tr_ema    // Positive = translation improving
d_data = Δ_data / S_data_ema  // Positive = data pain increasing

if (data_pain AND NOT tr_gain):
    decrease meta_level  // Back off
else if (tr_gain AND NOT data_pain):
    increase meta_level  // Push harder
else:
    hold current level
```

### 4. Stability Mechanisms

- **EMA smoothing**: All metrics use exponential moving averages (α=0.2) to filter noise
- **Cooldown**: After each level change, wait 2 epochs before adjusting again
- **Max step**: Only change by ±1 level per epoch

---

## Controller Type 2: Bandit Controller (`type = bandit`)

The bandit controller uses a **multi-armed bandit** approach where each protection level is an "arm":

### Algorithm

```
1. Each level L has a Q-value: Q[L] = estimated IPC at level L
2. Selection uses ε-greedy: 
   - With probability (1-β): pick argmax(Q)
   - With probability β: explore uniform random level
3. IPC reward is measured during epochs at the selected level
4. Q-values updated via exponential moving average
```

### Probing Mechanism

Rather than changing every epoch, the bandit uses a **probe-based** approach:

```
Every probe_period epochs:
    1. Start a "probe" of length probe_length epochs
    2. During probe: collect IPC samples at selected level
    3. At end of probe:
       - If IPC dropped > abort_threshold: ABORT (revert to best known level)
       - Otherwise: COMMIT (update Q-value, stay at level)
```

### Key Parameters

| Parameter | Description | Typical Value |
|-----------|-------------|---------------|
| `beta` | Exploration probability | 0.1 (10% exploration) |
| `probe_period` | Epochs between probes | 10 |
| `probe_length` | Duration of each probe | 5 epochs |
| `abort_threshold` | IPC drop to trigger abort | 0.05 (5% drop) |
| `penalty_factor` | Q-value penalty on abort | 0.1 |

### Why Bandit?

- **No domain knowledge required**: Only uses IPC, not translation/data stall breakdown
- **Learns from experience**: Adapts to workload-specific optimal level
- **Handles non-stationary workloads**: Continuous exploration via β

### Statistics Output

Bandit controller outputs stats with prefix `mplru-bandit.*`:

```
mplru-bandit.epochs = 583           # Total epochs
mplru-bandit.probes-started = 58    # Number of probes initiated
mplru-bandit.probes-aborted = 35    # Probes aborted (IPC dropped too much)
mplru-bandit.probes-committed = 23  # Probes committed (IPC acceptable)
mplru-bandit.level-X-time = ...     # Epochs spent at each level
mplru-bandit.Q-level-X = ...        # Final Q-value for each level
```

---

## Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                         MimicOS                                  │
│  ┌─────────────────────────────────────────────────────────┐    │
│  │  PerCoreStats (centralized statistics)                   │    │
│  │  - translation_latency, data_access_latency             │    │
│  │  - l2_tlb_hits, l2_tlb_misses, cycles                   │    │
│  └─────────────────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────────────────┘
         ▲                                    │
         │ updateTranslationStats()           │ getPerCoreStats()
         │ updateDataAccessStats()            ▼
┌────────┴────────┐                ┌──────────────────────┐
│  MMU / MemMgr   │                │  MPLRUController     │
│  (stats only)   │                │  - processEpoch()    │
└─────────────────┘                │  - getMetaLevel()    │
                                   └──────────────────────┘
                                              │
                                              │ tryProcessEpoch()
                                              │ getMetaLevel()
                                              ▼
                                   ┌──────────────────────┐
                                   │  CacheSetMPLRU       │
                                   │  (replacement policy)│
                                   └──────────────────────┘
```

**Key design principle**: MMUs and memory managers only update statistics. The replacement policy queries the controller and triggers epoch processing.

## Configuration

Add to your `.cfg` file:

### Base MPLRU Configuration

```ini
[perf_model/nuca]
replacement_policy = mplru

# Note: config path is under /cache because Cache class passes "perf_model/nuca/cache" as cfgname
[perf_model/nuca/cache/mplru]
mode = adaptive              # disabled | always | adaptive
mpki_threshold = 1.0         # Legacy, not used in adaptive mode

[perf_model/nuca/cache/mplru/controller]
enabled = true
type = delta                 # delta | bandit
epoch_instructions = 100000  # Instructions between controller epochs
```

### Delta Controller Configuration

```ini
[perf_model/nuca/cache/mplru/controller]
type = delta
tr_share_min = 0.01          # Min translation share to engage (1%)
rho_min = 0.02               # Min tr/data ratio to engage (2%)
meta_level_start = 2         # Starting level when engaged
meta_level_max = 5           # Maximum meta_level (0-5)
data_pain_frac = 0.05        # Data pain threshold (5% increase)
tr_gain_frac = 0.03          # Translation gain threshold (3% decrease)
cooldown_epochs = 2          # Epochs between adjustments
max_step_per_epoch = 1       # Max level change per epoch
ema_alpha = 0.2              # EMA smoothing factor
explore_epochs = 3           # Exploration phase length
severe_data_pain_mult = 2.0  # Multiplier for severe pain during exploration
engage_threshold_epochs = 2  # Epochs above threshold to engage
disengage_threshold_epochs = 4  # Epochs below threshold to disengage
```

### Bandit Controller Configuration

```ini
[perf_model/nuca/cache/mplru/controller]
type = bandit
beta = 0.1                   # Exploration probability (10%)
probe_period = 10            # Epochs between probes
probe_length = 5             # Epochs per probe
abort_threshold = 0.05       # IPC drop to trigger abort (5%)
penalty_factor = 0.1         # Q-value penalty on abort
```

## Files

| File | Purpose |
|------|---------|
| `mplru_controller.h` | Controller interface and state definition |
| `mplru_controller.cc` | Delta controller implementation (Δ-logic) |
| `mplru_bandit_controller.h` | Bandit controller interface |
| `mplru_bandit_controller.cc` | Bandit controller implementation (Q-learning) |
| `cache/cache_set_mplru.h` | Cache set interface |
| `cache/cache_set_mplru.cc` | Replacement policy implementation |

## Expected Behavior

### Delta Controller

**When translation stalls are near zero:**
- `engaged` stays `false`
- `meta_level` stays at 0 or 1
- Behaves like vanilla LRU

**When translation stalls are significant:**
- `engaged` becomes `true` (after hysteresis)
- `meta_level` jumps to `meta_level_start` (fast start)
- Exploration phase pushes higher

**When meta_level is too aggressive:**
- Data stalls rise significantly
- Controller detects `data_pain && !tr_gain`
- `meta_level` decreases within 1-3 epochs

**Under stable conditions:**
- `meta_level` settles (not bouncing every epoch)
- Cooldown prevents oscillations

### Bandit Controller

**During exploration phase (high β or early probes):**
- Tries different levels to gather Q-value estimates
- May see IPC fluctuations

**After learning converges:**
- Q-values stabilize
- Best level has highest Q-value
- Most time spent at `argmax(Q)`

**When workload changes:**
- Continued exploration (β) allows adaptation
- May see more aborts as old Q-values become stale
- Eventually re-learns optimal level

**Interpreting abort ratio:**
- High abort ratio (>50%): Workload is volatile or levels differ significantly
- Low abort ratio (<20%): Workload is stable, Q-values accurate

## Debugging

### Delta Controller Stats

The controller prints statistics at cleanup:

```
[MPLRUController] Core 0 final stats: engaged=1234 disengaged=567 exploration=45 up=23 down=12 final_level=2
```

Key metrics to check:
- **engaged vs disengaged**: High engaged ratio means translation was significant
- **up vs down**: Should be roughly balanced if controller found equilibrium
- **final_level**: Where the controller settled

### Bandit Controller Stats

Stats are recorded in `sim.stats` with prefix `mplru-bandit`:

```
mplru-bandit.epochs = 583
mplru-bandit.probes-started = 58
mplru-bandit.probes-aborted = 35
mplru-bandit.probes-committed = 23
mplru-bandit.level-0-time = 12
mplru-bandit.level-1-time = 45
mplru-bandit.level-2-time = 89
mplru-bandit.level-3-time = 320
mplru-bandit.level-4-time = 78
mplru-bandit.level-5-time = 39
mplru-bandit.Q-level-0 = 1.234
mplru-bandit.Q-level-1 = 2.456
mplru-bandit.Q-level-2 = 2.891
mplru-bandit.Q-level-3 = 3.343
mplru-bandit.Q-level-4 = 2.123
mplru-bandit.Q-level-5 = 1.876
```

Key metrics to check:
- **probes-aborted / probes-started**: Abort ratio, high means volatile workload
- **level-X-time**: Where controller spent most time (should match best Q-value)
- **Q-level-X**: Highest Q-value indicates optimal level for workload

## References

- Virtuoso: Address Translation Mechanisms (internal)
- NUCA cache design principles
- Adaptive replacement policies (ARC, DRRIP, etc.)
