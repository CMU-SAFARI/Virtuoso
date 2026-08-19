# Porting MimicOS to another simulator

MimicOS exists in three shapes. Knowing which one you are looking at saves
hours:

1. **Sniper-internal** — a C++ class compiled into the Sniper binary
   (`simulator/sniper/common/system/memory_management/mimicos.{h,cc}`). Always
   present; handles faults by direct function call.
2. **Standalone kernel** — `mimicos/build/startup_mimicos`, a separate process
   running under SDE/PIN that talks to Sniper through magic instructions. This
   is the one `docs/agent_notes/userspace_mimicos_walkthrough.md` describes.
3. **Embedding library** — `libmimicos.a`, a simulator-agnostic C++ library
   that a host simulator links directly. **This document is about shape 3.**

Shapes 1 and 2 are both tied to Sniper. Shape 3 is not tied to anything: the
only things crossing the boundary are integers and POD structs, declared in
`mimicos/include/mimicos_embed.h`. Porting MimicOS to a fourth simulator means
implementing an adapter against that one header.

Two adapters ship with the artifact, as patch series against pinned upstream
releases:

| Simulator | Upstream pin | What MimicOS owns |
|---|---|---|
| ChampSim | `06de8d3` | physical allocation, page tables, both page sizes in the TLBs, the fault handler's own instructions |
| gem5 (syscall-emulation mode) | `v24.1.0.3` | physical allocation, both page sizes in the TLB, minor-fault cost |

The difference is not arbitrary. ChampSim has a real `PageTableWalker` that
issues PTE reads into the cache hierarchy, so MimicOS's page tables are
observable there. gem5's SE mode resolves translation functionally through
`EmulationPageTable`, with no memory traffic, so page-table geometry has
nothing to show up in — see [What is *not* modelled](#9-what-is-not-modelled).

---

## 1. Building the library

```bash
make -C mimicos lib-static     # mimicos/build/libmimicos.a
make -C mimicos lib            # also builds libmimicos.so
make -C mimicos check-embed    # run the library's own tests
```

The library shares the allocators, page-table code and fault-calibration model
verbatim with the standalone kernel. It excludes exactly one translation unit,
`src/mimicos/mimicos.cc`, which is where the magic-instruction protocol lives.

One deliberate difference: the library is compiled **without**
`-DMIMICOS_CALIBRATION_O0`. That flag exists so the standalone kernel's
allocator code keeps the instruction count we calibrated against Linux when SDE
simulates it instruction by instruction. Embedded, MimicOS executes natively
inside the host simulator and is never simulated, so forcing `-O0` would only
slow the host down. See [section 5](#5-the-fault-cost-model-means-something-different-when-embedded).

---

## 2. The API

The whole surface is `mimicos::Kernel`:

```cpp
#include "mimicos_embed.h"

std::string err;
auto kernel = mimicos::Kernel::create_from_ini("mimicos.ini", &err);
if (!kernel) { /* err explains why */ }

mimicos::FaultRequest req;
req.vaddr = 0x7f0000001000;
req.asid  = cpu_id;          // one page table per address space

auto t = kernel->translate(req);
// t.ppn             4 KiB-granular frame *containing* vaddr
// t.page_size_bits  12, 21 or 30
// t.faulted         whether this call resolved a fault
// t.fault_latency_ns calibrated cost of that fault
```

| Call | Use it for |
|---|---|
| `translate(req)` | resolve an address, faulting it in on first touch |
| `probe(asid, va, …)` | read-only lookup; never allocates |
| `pte_address(req, level, …)` | physical address of the PTE at one level of the walk |
| `walk(req)` | the whole sequence of PTE addresses, root first |
| `root_table_paddr(asid)` | the CR3-equivalent |
| `leaf_level(asid, va)` | where the walk terminates: 1 = 4 KiB, 2 = 2 MiB, 3 = 1 GiB |
| `minor_fault_latency_ns(…)` | cost of a fault of a given shape |
| `enable_mapping_trace(path)` | dump every mapping decision, for equivalence testing |

`t.ppn` is always 4 KiB-granular and already offset into a huge frame, so a
host can use it directly. This matches the convention Sniper's MMU applies in
`mmu_base.cc` (`pa = ppn * 4096 + (va & ((1 << page_size) - 1))`).

A `Kernel` is **not** internally synchronised. A host that translates from
several threads must serialise, as the standalone kernel does with its
`m_alloc_mutex`.

---

## 3. ChampSim

### Build and run

```bash
# 1. build the library
make -C mimicos lib-static

# 2. build ChampSim against it
cd simulator/ChampSim
./config.sh champsim_config.json
make MIMICOS_HOME=$PWD/../../mimicos -j

# 3. run
MIMICOS_CONFIG=../../mimicos/configs/embedded_4gb_thp.ini \
  ./bin/champsim --deadlock-cycle 100000 -w 200000 -i 1000000 trace.champsimtrace.xz
```

Without `MIMICOS_HOME`, `src/vmem_mimicos.cc` compiles to stubs, nothing links
against MimicOS, and behaviour is bit-identical to stock ChampSim. The config
can also be set per-run in the ChampSim config JSON:

```json
"virtual_memory": { "num_levels": 5, "mimicos_config": "/path/to/mimicos.ini" }
```

`MIMICOS_CONFIG` in the environment overrides the JSON, so one configured
binary can be pointed at different MimicOS setups.

### What it changes

`VirtualMemory::va_to_pa` and `get_pte_pa` delegate to MimicOS, so the existing
`PageTableWalker` fetches MimicOS-owned PTE addresses through the real cache
hierarchy. `ptw.cc` also learned to end a walk early on a huge page: it used to
assume the last step is always level 0, and now asks
`vmem->min_translation_level()`. The stock model returns 0 unconditionally.

### Three things that bit us, and will bit you

**`num_levels` must match.** ChampSim's page-table depth is baked into the PSCL
geometry and the walker's level counting. The adapter forces MimicOS to build a
tree of exactly that depth. If your MimicOS INI carries its own
`[page_table] levels`, make it agree — a mismatch produces different
page-table frame counts and silently different results. Mapping traces record
the geometry so `check_equivalence.py` can catch it.

**A realistic fault trips the deadlock detector.** ChampSim's detector fires
after 500 cycles with no progress, which comfortably covers its own 200-cycle
default `minor_fault_penalty`. A measured minor fault is microseconds — 
thousands of cycles at a multi-GHz clock — and looks exactly like a deadlock.
`--deadlock-cycle` now raises the window. Do not shorten the fault instead.

**Short runs are fault-dominated.** With a 2 µs fault model, 605.mcf over
200 k instructions spends most of its cycles in cold-start faults (IPC 0.0139
versus 0.1686 at ChampSim's default penalty). That is not a bug; it means short
ChampSim runs are not a sensible place to read absolute IPC off a MimicOS
configuration. Warm up properly, or hold the fault cost fixed when the question
is about placement.

### What it buys you

Measured on `605.mcf_s-1644B`, 1 M instructions after 200 k warmup, with the
fault cost held at ChampSim's default so only *placement* differs:

| Configuration | IPC | LLC misses |
|---|---|---|
| stock (shuffled free list) | 0.1001 | 116 552 |
| MimicOS, `baseline` allocator | 0.1597 | 89 201 |
| MimicOS, `reserve_thp` | 0.1582 | 89 029 |

The gap is physical locality: stock ChampSim shuffles its physical page free
list by default (`"randomization": 1`), MimicOS's buddy allocator does not.
This is a placement effect and not frame aliasing —
`mimicos/tests/embed_smoke.cc` checks that distinct virtual pages never share a
frame, across all three allocators.

---

## 4. gem5, syscall-emulation mode

### Build and run

```bash
make -C mimicos lib-static

cd simulator/gem5
MIMICOS_HOME=$PWD/../../mimicos scons build/X86/gem5.opt -j$(nproc)

build/X86/gem5.opt configs/example/mimicos_se.py \
    --mimicos-config ../../mimicos/configs/embedded_4gb_thp.ini
```

Without `MIMICOS_HOME`, `src/sim/mimicos_binding.cc` compiles to stubs and gem5
is unchanged. On the configuration side the entire integration is one line:

```python
system.workload = SEWorkload.init_compatible(binary)
system.workload.mimicos_config = "/path/to/mimicos.ini"
```

### What it changes

Stock SE mode allocates physical frames with a bump pointer (`MemPool`) and
resolves minor faults functionally, in zero simulated time. Neither models
anything: placement is whatever order pages happened to be touched in, and a
fault is free.

With MimicOS attached, `Process::allocateMem` resolves pages **one at a time**
through MimicOS rather than taking one contiguous run from the pool, because
MimicOS places frames as a function of the virtual address — its buddy
allocator and THP reservation both key off it. Physical contiguity then
reflects what the allocator actually decided.

`X86ISA::PageFault::invoke` charges the calibrated cost by quiescing the
faulting thread for that long, the same mechanism `m5_quiesce_ns` uses. Without
MimicOS the latency is zero and the code path is a no-op.

### Physical address space

MimicOS allocates within its own space, and gem5 still hands out frames from
the `MemPool` for its own structures — x86 descriptor tables, GPU buffers. To
keep the two from colliding, `SEWorkload::setSystem` reserves a contiguous
region the size of MimicOS's pool from the `MemPool` up front and tells the
binding its base. **The system's memory must therefore be larger than the
MimicOS config's `[pmem_alloc] memory_size`**; gem5 fails at startup with a
clear message if it is not.

### Other ISAs

The allocator hook is ISA-independent — it lives in `Process::allocateMem`.
The *fault latency* hook is not: it is in `src/arch/x86/faults.cc`, because SE
page faults are delivered per-ISA. Porting it is about five lines; RISC-V's
equivalent is in `src/arch/riscv/tlb.cc` and SPARC's in
`src/arch/sparc/faults.cc`, both of which call `Process::fixupFault`.

---

## 5. The fault cost model, and when it is used at all

**The `[fault_calibration]` numbers are a fallback.** They are not used at all
in the modes where MimicOS's own instructions run:

| mode | uses `[fault_calibration]`? |
|---|---|
| ChampSim, `MIMICOS_FAULT_TRACE` set (handler replayed) | no |
| gem5, `--mimicos-server` (kernel in the guest) | no |
| ChampSim or gem5 with only `mimicos_config` | yes |

Proven rather than asserted: with the handler replayed, multiplying every
calibration value by 5 000 leaves the cycle count *identical* (433 533 either
way on 605.mcf). Without it, the same change makes the run so slow it does not
finish. In ChampSim the latency is an `else` on the replay branch; in gem5's
guest mode the in-process binding's `allocate()` is never called at all.

So when MimicOS actually executes, the cost comes from the handler. The
calibration values are a simulator-specific offset for the analytical path
only — the numbers in `embedded_4gb_thp.ini` are illustrative and matter only
there.

## 5b. What the analytical path means when embedded

This is the single easiest thing to get wrong.

In the **standalone kernel**, the `[fault_calibration]` values are *corrections*
added on top of the fault-handling code that SDE actually simulates instruction
by instruction. The bulk of the cost comes from simulating MimicOS's own
execution; the calibration closes the remaining gap against real hardware.

In the **embedding library**, MimicOS runs natively inside the host simulator
and is never simulated. There is no execution to charge, so
`[fault_calibration]` is the *entire* cost model.

Every config in `mimicos/configs/` that predates the library has these values
at zero, because zero was the right correction for a Sniper run. Carried over
unchanged, they would make page faults free. Both adapters detect this, warn,
and fall back to the host simulator's own minor-fault penalty rather than
silently pricing faults at nothing.

`mimicos/configs/embedded_4gb_thp.ini` is a worked example with a per-phase
decomposition summing to ~2 µs. **Replace those numbers with your own
measurements before quoting absolute latencies** — `tools/fit_fault_model.py`
produces this section from eBPF traces.

---

## 6. Cross-simulator equivalence testing

ChampSim, gem5 and Sniper cannot consume the same trace format, so they can
never be handed a byte-identical input stream. What *can* be established is
that each adapter is a pass-through — that it does not perturb MimicOS's
decisions.

The mechanism is a mapping trace. With `MIMICOS_MAPPING_TRACE` set, the kernel
writes one CSV row per mapping decision, prefixed by a metadata line recording
the geometry and allocator it ran under. That trace's `(asid, vpn)` sequence is
then replayed against libmimicos on its own, and the two are compared:

```bash
make -C mimicos replay

MIMICOS_CONFIG=mimicos/configs/embedded_4gb_thp.ini \
MIMICOS_MAPPING_TRACE=/tmp/champsim_mapping.csv \
  simulator/ChampSim/bin/champsim --deadlock-cycle 100000 -w 20000 -i 100000 trace.xz

python3 mimicos/tests/check_equivalence.py \
    --config mimicos/configs/embedded_4gb_thp.ini \
    --trace  /tmp/champsim_mapping.csv
```

There are two different claims here, and they need different tests.

**One simulator against the library** (`--config` / `--trace`) replays that
simulator's own fault sequence against libmimicos alone and compares row for
row. Given one sequence of faults, MimicOS must decide the same things whether
or not a simulator is driving it. Placement — `vpn`, `base_ppn`,
`page_size_bits` — must match exactly. Cost attribution (`pt_frames`,
`fault_latency_ns`) must match *in total*: a host that runs concurrent
page-table walks can create an interior table during one walk and finish a
different walk first, so which fault is billed varies. On 605.mcf, 6 of 2 987
rows are billed differently and the totals are identical. gem5, which faults
sequentially, matches row for row.

**Two simulators against each other** (`--compare --unordered`) cannot expect
row-for-row equality, and does not need it. They do not fault in the same
order, and MimicOS hands out frames as faults arrive, so a different order
means the same pages get different frames — exactly as a real OS would. What
must hold is what does not depend on order:

- both map the same set of pages
- each page gets the same size in both
- no two unrelated pages share physical memory
- no page moves to a different frame within a run

Promotion is not aliasing: when a region is upgraded to 2 MiB, the huge mapping
starts on the frame the region's first 4 KiB page already had, so their physical
ranges overlap on purpose. The check only flags an overlap when the *virtual*
ranges are disjoint too.

`--compare a.csv b.csv` diffs two simulators' traces directly, for the cases
where their virtual-address streams really are the same.

### Results

gem5 and ChampSim, driven by the same workload, on a 16 MiB working set:

| config | pages mapped | page sizes | frames |
|---|---|---|---|
| 4 KiB | 4 307 both | all 4 KiB | 4 307 both, none shared |
| mixed | 2 268 both | 2 260 × 4 KiB, 8 × 2 MiB, identical per page | 4 308 both |
| 2 MiB | 12 both | all 2 MiB | 6 144 both |

`tests/cross_simulator_check.sh` runs this. Note it bounds ChampSim's
instruction count to the generated trace's length: ChampSim loops a trace that
runs out, so `-i` larger than the trace never terminates.

Every allocator and both hosts also agree with the reference:

| Host | Allocator | Trace | Mappings | Placement | Attribution |
|---|---|---|---|---|---|
| ChampSim | `baseline` | 605.mcf | 4 140 | identical | 6 rows reordered, totals equal |
| ChampSim | `baseline` | 623.xalancbmk | 1 476 | identical | totals equal |
| ChampSim | `linux_buddy_anon` | 605.mcf | 4 140 | identical | totals equal |
| ChampSim | `linux_buddy_anon` | 623.xalancbmk | 1 476 | identical | totals equal |
| ChampSim | `reserve_thp` | 605.mcf | 4 085 | identical | totals equal |
| ChampSim | `reserve_thp` | 623.xalancbmk | 1 431 | identical | totals equal |
| gem5 SE | `reserve_thp` | hello | 152 | identical | **exactly** identical |

The last row is the useful contrast. gem5 resolves faults sequentially, so
nothing can be billed out of order and even the per-row attribution matches.
ChampSim runs concurrent page-table walks, so a handful of rows are billed to a
different fault while the totals stay equal. That is the difference the
placement/attribution split exists to express — without it, ChampSim would look
like it had a bug and gem5 would look like it was not being tested hard enough.

`reserve_thp` produces fewer rows than the other two for the same trace because
a promoted 2 MiB page covers 512 virtual pages with one mapping.

---

## 7. Page sizes

Both simulators now hold 4 KiB and 2 MiB entries in their TLBs. Before this,
a 2 MiB page cost one TLB entry per 4 KiB page inside it, so huge pages bought
nothing in the TLB and only shortened the ChampSim walk.

A TLB indexes on the page number, so an entry for a 2 MiB page cannot be found
by a lookup that indexes on the 4 KiB page number. Both implementations
therefore probe twice per lookup, and entries record which size they are.

**ChampSim** takes the shape from the config:

```json
"DTLB": { "sets": 16, "ways": 4,
          "huge_page_shift": 21, "huge_page_sets": 8, "huge_page_ways": 4 },
"STLB": { "sets": 128, "ways": 12,
          "huge_page_shift": 21, "huge_page_sets": 0, "huge_page_ways": 0 }
```

`huge_page_ways > 0` gives huge entries their own array, which is what an L1
DTLB does. `huge_page_ways == 0` keeps them in the main array sharing capacity,
which is what an L2 STLB does. `huge_page_shift: 0` — the default — turns the
whole thing off, so a config that says nothing behaves exactly as before.

**gem5** needed almost nothing: its x86 TLB is a trie that already supports
variable page sizes for full-system mode. SE mode was throwing the information
away, passing the full key width to `trie.insert()` instead of the entry's own
size, and hardcoding `logBytes` to `PageShift`. The page table now records the
real page size in two new mapping flags and the TLB installs one entry for it.

One hole worth knowing about: gem5's `concAddrPcid()` packs the pcid into the
low 12 bits and the trie only matches the top `64 - logBytes`, so a large entry
does not distinguish address spaces. Full-system mode has the same hole. It does
not bite in SE mode, where each CPU's TLB serves one process. An earlier version
of this patch fell back to 4 KiB whenever the pcid was non-zero, which turned
the feature off entirely — SE mode runs with pcid `0x64`.

### Measured

Three configs pin down the page size: `embedded_4gb_4k.ini` (the baseline
allocator never promotes), `embedded_4gb_2m.ini` (reserve_thp promotes on first
touch) and `embedded_4gb_mixed.ini` (promote once a region is half used).

ChampSim, 605.mcf, 150 k instructions:

| config | IPC | DTLB misses | STLB misses |
|---|---|---|---|
| 4 KiB | 0.01324 | 29 650 | 3 650 |
| mixed | 0.01338 | 28 452 | 3 493 |
| 2 MiB | 0.08224 | 5 654 | 342 |

gem5, a 32 MiB working set walked four times:

| config | dtb read misses | dtb write misses |
|---|---|---|
| 4 KiB | 32 796 | 16 396 |
| 2 MiB | 2 | 35 |

## 8. Running the fault handler instead of charging for it

A fixed latency says nothing about what the fault handler does to the caches
and the TLBs, which is most of what it costs. ChampSim can run the handler's
real instructions instead.

Record once:

```bash
make -C mimicos fault-driver
# ChampSim's own PIN tracer, built against the SDE pinkit already in this tree
make -C simulator/ChampSim/tracer/pin      PIN_ROOT=$PWD/simulator/sniper/sde_kit/pinkit obj-intel64/champsim_tracer.so

setarch $(uname -m) -R simulator/sniper/sde_kit/pinkit/pin     -t simulator/ChampSim/tracer/pin/obj-intel64/champsim_tracer.so     -o /tmp/mimicos_faults.champsimtrace -t 20000000     -- mimicos/build/fault_driver_static mimicos/configs/embedded_4gb_4k.ini 64 1

python3 mimicos/tests/extract_fault_window.py     /tmp/mimicos_faults.champsimtrace <marker_addr> /tmp/one_fault.champsimtrace
```

`fault_driver` prints the marker address; record with ASLR off (`setarch -R`)
or it moves. Then:

```bash
MIMICOS_CONFIG=... MIMICOS_FAULT_TRACE=/tmp/one_fault.champsimtrace ./bin/champsim ...
```

Every fault now replays those instructions into the core ahead of the trace.
They fetch, decode and execute like any others. The fixed latency is dropped
while a recording is in use, since charging both counts the same work twice.
Kernel instructions are marked so they do not count towards the instruction
budget or the reported IPC — without that, a run with more page faults looks
like it retired more work.

What a minor fault actually costs, over 64 faults:

| allocator | instructions per fault |
|---|---|
| baseline, 4 KiB | 494–633, median 565 |
| reserve_thp, no promotion | 878 |
| reserve_thp, promoting to 2 MiB | 1 268–1 426, median 1 426 |

The first fault of any run is 20–30 k instructions because it warms the
allocator's free lists, which is why the extractor takes a median rather than a
fixed index. Watch the stride when recording 2 MiB faults: with a one-page
stride only the first access faults and the other 63 land inside the same huge
page, so what gets recorded is the hit path.

**MimicOS's own accesses are not translated.** MimicOS is what answers a
translation, so routing the kernel's accesses through the TLBs would ask it to
translate for itself. Replayed instructions are marked `is_kernel`, which
becomes `is_translated` on the cache-bus request, so their addresses go to the
caches as physical and never reach a TLB or the walker. They still occupy the
pipeline and still pollute the caches — only translation is skipped.

Getting this wrong is not loud. Before the bypass, replaying the handler took
DTLB accesses from 57 k to 465 k, which looked like a finding about the
handler's TLB footprint and was nothing of the sort. The check that catches it
is the mapping count, not the stats: with and without replay MimicOS must
produce **exactly the same number of mappings** (3030 either way on 605.mcf).
Any kernel address reaching MimicOS would add some.

On 605.mcf at 100 k instructions:

| | IPC | DTLB accesses | DTLB misses |
|---|---|---|---|
| stock | 0.08553 | 57 541 | 20 093 |
| 4 KiB, 2 µs fixed latency | 0.01285 | 57 191 | 20 126 |
| 4 KiB, handler replayed | 0.0794 | 62 065 | 18 808 |
| 2 MiB, 2 µs fixed latency | 0.06623 | 61 101 | 4 368 |
| 2 MiB, handler replayed | 0.1459 | — | — |

The fixed latency is far more pessimistic than the ~565 instructions the
handler really runs. **Use the measured instruction counts above rather than
the example latencies in `embedded_4gb_thp.ini`**, which are illustrative only.

gem5 does it a different way — see the next section.

## 9. gem5: MimicOS as a guest process

ChampSim is trace-driven and cannot execute anything, so it replays a recording.
gem5 runs real binaries, so it can do the honest thing: run MimicOS as a
process and hand it each fault.

```bash
make -C mimicos gem5-server

build/X86/gem5.opt configs/example/mimicos_se.py \
    --mimicos-config   ../../mimicos/configs/embedded_4gb_4k.ini \
    --mimicos-server   ../../mimicos/build/mimicos_gem5_server \
    <application>
```

The application runs on cpu0 and the kernel on cpu1. The kernel needs a CPU of
its own because it has to run while the faulting thread is parked.

The exchange is two m5ops, `0x71` and `0x72`:

```c
for (;;) {
    uint64_t va = m5_mimicos_wait();     /* parks until a fault arrives */
    if (!va) continue;
    auto t = kernel->translate({va, ...});   /* real MimicOS, real instructions */
    m5_mimicos_reply(t.ppn, t.page_size_bits);
}
```

Everything between the two m5ops executes as guest instructions: the buddy
allocator, the reservation logic, the radix page table. That is the difference
from `mimicos_config` alone, where the simulator calls into MimicOS and the cost
of a fault can only be a number from `[fault_calibration]`.

A fault takes two trips through the fault handler. The first hands it to the
kernel and parks the thread; the second, once the kernel has answered, goes down
gem5's normal `fixupFault` path so the region checks in `MemState` still run —
only the *frame* comes from the kernel. Skipping that path would have meant a
bad access getting silently mapped instead of faulting.

Measured on a 1 MB working set: **260 faults, all 260 served by the kernel**,
the workload's checksum matching a native run, and the simulation exiting on
its own once the application finishes.

Four things had to be right, and each was wrong first. They are worth knowing
because three of them fail *quietly*:

- **Never fall back.** The first version declined to hand over a fault while
  the kernel was still starting up, and the run used gem5's own allocator for
  all 260 faults while looking perfectly healthy. Check the handoff count, not
  whether the run completed.
- **Park, do not spin.** `suspend()` lets the pseudo-instruction return and the
  thread keeps fetching, so an idle kernel span until the run hit its tick
  limit. `quiesce()` is what `m5_quiesce` uses.
- **The faulting instruction is re-executed as soon as `invoke()` returns**,
  before the park takes effect, so the same address comes straight back. That
  is a retry, not a second fault.
- **The kernel faults too**, on its own text and stack, and cannot serve those
  itself — it is not running yet when they happen.

Two more things worth knowing:

- **The kernel process's own memory does not come from MimicOS.** It comes from
  gem5's `MemPool`, like any other process. Serving it from the copy of MimicOS
  inside gem5 would put two allocators on the same physical region and they
  would hand out the same frames — the frame guard in section 6 exists partly
  because that is easy to do by accident.
- **Threads queue.** Any number can fault at once; each parks on its own and
  is woken by its own answer. The kernel takes them one at a time, which is
  what a real handler does anyway. Give the application one CPU per thread
  *plus one for main* with `--app-threads`: syscall emulation hands a new
  thread the first idle context it finds, and one short means
  `pthread_create` returns EAGAIN and that thread's work is missing from the
  result with nothing in the log to say so. That is gem5's behaviour, not
  MimicOS's — it reproduces with the kernel disabled.

  Four threads over a 4 MB mapping: 1 037 faults queued and answered, up to
  three waiting at once, checksum matching a native run.

## 10. What is *not* modelled

Being explicit about this is more useful than the feature list.

- **gem5 SE mode has no page-table walk.** MimicOS's page tables are built and
  charged for (they consume kernel memory), but no PTE is ever fetched, because
  `EmulationPageTable` is a functional map. Huge pages therefore show up in
  gem5 only as physical *contiguity*, not as shorter walks or TLB reach.
- **Without `--mimicos-server`, gem5's fault cost is analytical.** The guest
  kernel is opt-in; with only `--mimicos-config` the simulator calls into
  MimicOS and charges `[fault_calibration]`. The measured instruction counts in
  section 8 are the right basis for those numbers.
- **The first walk of a page always runs to full depth.** Nothing is mapped
  yet, so there is no leaf to terminate at; the walk shortens from the second
  access onward. As a side effect the first walk of a region that later becomes
  a 2 MiB page allocates one page-table frame a real OS would not have needed.
- **The frame guard is a check, not a fix.** libmimicos keeps a bitmap of
  frames it has handed out and reports `aliased_frames` if one comes back twice;
  both adapters print it. It exists because this failure is otherwise silent —
  two pages sharing memory shrinks the working set and flatters every cache
  number downstream. It has fired once in this work, from a `libmimicos.a` that
  `ar` was rewriting while a simulator linked against it. Do not rebuild the
  library while a host is linking.
- **Check a result, not a counter.** Twice in building this, a run with
  healthy-looking fault counts was wrong: once because MimicOS never saw the
  faults at all, once because a thread's work went missing. Both were caught
  by an end-to-end check — the mapping count in the first case, the workload's
  checksum in the second — and neither by the fault counters, which looked
  fine.
- **Promotion does not shoot down stale 4 KiB TLB entries.** When MimicOS
  upgrades a region, entries already in the 4 KiB array stay there until they
  age out. They are harmless — a reserved region hands out frames inside the
  2 MiB frame it will later become, so the stale entry maps to the same physical
  address — but they occupy capacity a real TLB shootdown would have freed.
- **Replayed handler instructions are not translated at all.** Their addresses
  go to the caches as physical. That is deliberate — MimicOS is what answers a
  translation, so translating for itself is circular — but it does mean the
  kernel's own pages consume no page-table entries and appear in no TLB, which
  a real machine would not do.
- **Traces carry no mapping metadata.** Every fault is modelled as an anonymous
  write, in both adapters. File-backed and shared mappings — and the very
  different costs `feedback_rss_mix_vs_fault_mix` documents — are not
  distinguished. Drive MimicOS from a VMA-aware front end if that matters.
- **Major faults and swap are not reachable** through either adapter. MimicOS
  supports them; nothing in these two hosts asks for them.
- **The `Buddy` constructor still prints a few lines at startup.** Per-fault
  logging is gated behind `[mimicos] verbose` (off by default when embedded),
  but the constructor banners live in a header shared with Sniper's build and
  were left alone rather than risk that build.

---

## 11. Porting to a fourth simulator

1. Find the host's translation choke point — the function that turns a virtual
   page into a physical frame. In ChampSim that is `VirtualMemory::va_to_pa`;
   in gem5 SE mode it is `Process::allocateMem`.
2. Write a binding modelled on `simulator/ChampSim/src/vmem_mimicos.cc` or
   `simulator/gem5/src/sim/mimicos_binding.cc`. Both are ~170 lines, hide
   MimicOS behind a pimpl so the host's headers stay clean, and compile to
   stubs when the feature flag is off.
3. Decide where MimicOS's physical space lives relative to the host's own
   allocations, and make them disjoint. gem5 reserves a region from the
   `MemPool`; ChampSim simply stops using its free list.
4. Charge `fault_latency_ns` somewhere the host can express a stall, and check
   the host's deadlock/watchdog thresholds tolerate it.
5. If the host has a page-table walker, feed it `pte_address()` and
   `leaf_level()`. If it does not, skip both — you still get placement and
   fault cost, which is what gem5 SE mode gets.
6. Add `MIMICOS_MAPPING_TRACE` support and run `check_equivalence.py`. If
   placement matches the reference, the adapter is a pass-through.
