# How the MimicOS integration works

A short tour of what was added to gem5 and ChampSim, and why each piece
is there. For the full reference — API, configuration, limitations — see
[mimicos_portable.md](mimicos_portable.md).

---

## The idea

MimicOS is an OS memory manager: a buddy allocator, THP reservation and
promotion, radix page tables, a fault handler. Sniper runs it either
compiled in or as a separate process under SDE.

A simulator without it has to invent the same decisions. ChampSim hands out
physical frames from a shuffled free list; gem5's syscall-emulation mode uses
a bump pointer and resolves faults in zero simulated time. Neither models an
allocator, so questions about fragmentation, huge pages or fault cost cannot
be asked at all.

The integration replaces those invented decisions with MimicOS's real ones,
in both simulators, from one shared library.

```
                         ┌──────────────────────────┐
                         │   libmimicos             │
                         │   buddy allocator        │
                         │   THP reserve / promote  │
                         │   radix page tables      │
                         │   fault cost model       │
                         └───────────┬──────────────┘
                       mimicos_embed.h (ints and PODs only)
                     ┌───────────────┴───────────────┐
                     │                               │
            ┌────────┴─────────┐           ┌─────────┴──────────┐
            │ ChampSim         │           │ gem5 SE mode       │
            │ vmem_mimicos.cc  │           │ mimicos_binding.cc │
            │                  │           │ mimicos_broker.cc  │
            └──────────────────┘           └────────────────────┘
```

---

## 1. The library

`libmimicos.a` is MimicOS with the Sniper couplings removed. Its whole surface
is `mimicos::Kernel`:

| call | question it answers |
|---|---|
| `translate()` | which frame backs this page, and how big is the page |
| `pte_address()` | where does the page table entry for this level live |
| `leaf_level()` | how deep does the walk go |
| `page_shift()` | 4 KiB or 2 MiB, for the TLBs |

Nothing but integers and POD structs crosses the boundary, so an adapter needs
this header and nothing else.

It also keeps one bit per physical frame and reports `aliased_frames` if the
allocator ever hands the same frame out twice. That failure is silent
otherwise: two pages sharing memory shrinks the working set and flatters every
cache number downstream.

---

## 2. ChampSim

ChampSim reads traces and cannot execute anything, which shapes every choice.

**Allocation and page tables.** `VirtualMemory::va_to_pa` and `get_pte_pa`
call MimicOS instead of the built-in free list. Because `get_pte_pa` returns a
real physical address, ChampSim's existing page table walker fetches MimicOS's
entries through the cache hierarchy — allocator policy shows up in measured
traffic rather than being assumed away.

**Two page sizes in the TLBs.** A TLB is indexed on the page number, so an
entry for a 2 MiB page cannot be found by a lookup that indexes on the 4 KiB
page number. Every lookup therefore probes twice, and entries record which
they are. Configured per TLB: separate arrays for an L1, shared capacity for
an L2.

**The fault handler.** ChampSim cannot run MimicOS, so we record it once —
`fault_driver` under ChampSim's own PIN tracer — and replay those instructions
into the core's instruction queue at each fault. They fetch, decode, execute
and disturb the caches like any others. Measured: 565 instructions for a
4 KiB fault, 1426 when promoting to 2 MiB.

**Kernel instructions are marked.** Two things follow. They do not count
towards the program's IPC, or a run with more faults would look like it
retired more work. And their memory accesses skip address translation
entirely — MimicOS is what answers a translation, so sending its own accesses
through the TLBs would ask it to translate for itself.

```
trace ──► instruction queue ──► core ──► DTLB ──► STLB ──► PTW ──► caches
             ▲                                                      │
             │ handler instructions, on a fault                      │
             │                                              MimicOS decides
             └──────────────── VirtualMemory ◄──────────────  frames + PTEs
```

---

## 3. gem5, syscall-emulation mode

gem5 runs real binaries, so it can do the honest thing: run MimicOS.

**Allocation.** `Process::allocateMem` resolves one page at a time through
MimicOS, because MimicOS places frames from the virtual address.

**Two page sizes.** gem5's x86 TLB is already a trie that supports variable
page sizes for full-system mode; SE mode was throwing the information away.
The page table now records how big the real page is and the TLB installs one
entry for it. A 32 MiB working set goes from 32 796 data-TLB read misses to 2.

**The kernel in the guest.** With `--mimicos-server`, MimicOS runs as an
ordinary process on its own CPU and the simulator hands it each fault:

```
   application (cpu0)              simulator              kernel (cpu1)
   ──────────────────              ─────────              ─────────────
   touches a page
   fault ─────────────────────────► queue it
                                    park the thread
                                    wake the kernel ─────► m5_mimicos_wait()
                                                           returns the address
                                                           m5_mimicos_asid()
                                                           ── MimicOS runs ──
                                                           allocator, page
                                                           tables, all as
                                                           guest instructions
                                    remember the frame ◄─── m5_mimicos_reply()
                                    wake the thread
   runs again
   faults again ──────────────────► answer is waiting,
                                    normal path uses it
   continues
```

Two trips through the fault handler per miss. The second one goes down gem5's
ordinary `fixupFault` path so the region checks in `MemState` still run —
only the *frame* comes from the kernel. Resolving it directly would let a bad
access be mapped instead of faulting.

Any number of threads may fault at once; they queue and each is woken by its
own answer. The kernel takes them one at a time, as a real handler does.

---

## 4. What the two have in common

- One library, one config file. The same INI drives Sniper, ChampSim and gem5.
- Both are inert unless enabled. Without `MIMICOS_HOME` the MimicOS files
  compile to stubs and the host behaves exactly as upstream.
- MimicOS's own accesses never go through address translation, in either.
- The `[fault_calibration]` numbers are a fallback. Neither of the modes where
  MimicOS's instructions actually run touches them.

---

## 5. Checking it works

The thing that matters is not whether a run completes. Twice during this work
a run with healthy-looking fault counters was wrong — once MimicOS never saw
the faults at all, once a thread's work went missing — and both times only an
end-to-end check caught it.

```bash
# the library on its own
make -C mimicos check-embed

# does the simulator drive MimicOS the same way the library does alone?
python3 mimicos/tests/check_equivalence.py --config <ini> --trace <mapping.csv>

# do the two simulators agree with each other?
bash mimicos/tests/cross_simulator_check.sh <gem5.opt> <champsim> <workload>

# does the gem5 guest kernel actually serve the faults?
python3 mimicos/tests/check_gem5_se.py --gem5 <gem5.opt> --config <ini>
```

The equivalence check compares what does not depend on fault order: both
simulators must map the same pages, at the same sizes, one frame each. They do
not fault in the same order and do not have to — MimicOS hands out frames as
faults arrive, exactly as a real OS would.
