#!/usr/bin/env python3
"""Regenerate sim.stats for finished runs instead of re-running them.

Usage: regen_stats.py SNIPER_ROOT RUNDIR [RUNDIR ...]
For each RUNDIR whose simulation/sim.stats is missing or lacks the IPC counters,
if simulation/sim.stats.sqlite3 holds the final ('stop') snapshot and no
run-sniper currently holds RUNDIR/.run-sniper.lock, rebuild sim.stats with
tools/dumpstats.py.  Prints one line per RUNDIR: REGENERATED / SKIP <reason>.
Exit code 0 always (callers decide from the per-dir result)."""
import fcntl, os, sqlite3, subprocess, sys

def valid(stats):
    try:
        with open(stats) as f:
            return any(l.startswith('performance_model.cycle_count =') for l in f)
    except OSError:
        return False

def main():
    root = sys.argv[1]
    for d in sys.argv[2:]:
        sim = os.path.join(d, 'simulation')
        stats, db = os.path.join(sim, 'sim.stats'), os.path.join(sim, 'sim.stats.sqlite3')
        if valid(stats):
            print(f'SKIP valid {d}'); continue
        if not os.path.exists(db):
            print(f'SKIP no-db {d}'); continue
        # a run still writing this directory holds the lock: never touch it
        lock = os.path.join(d, '.run-sniper.lock')
        if os.path.exists(lock):
            try:
                fd = open(lock, 'a+')
                fcntl.lockf(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
                fcntl.lockf(fd, fcntl.LOCK_UN); fd.close()
            except OSError:
                print(f'SKIP running {d}'); continue
        try:
            con = sqlite3.connect(f'file:{db}?mode=ro', uri=True)
            prefixes = {r[0] for r in con.execute('select prefixname from prefixes')}
            con.close()
        except sqlite3.Error as e:
            print(f'SKIP db-error({e}) {d}'); continue
        if 'stop' not in prefixes or 'roi-end' not in prefixes:
            print(f'SKIP incomplete-db {d}'); continue
        tmp = stats + '.regen'
        with open(tmp, 'w') as out:
            rc = subprocess.run([os.path.join(root, 'tools', 'dumpstats.py'), '-d', sim],
                                stdout=out, stderr=subprocess.DEVNULL).returncode
        if rc == 0 and valid(tmp):
            os.replace(tmp, stats); print(f'REGENERATED {d}')
        else:
            try: os.unlink(tmp)
            except OSError: pass
            print(f'SKIP dumpstats-failed {d}')

if __name__ == '__main__':
    main()
