"""Shared helpers for single-core AE parsers."""
import glob, math, os

def ipc(path):
    i=c=None
    try:
        for l in open(path):
            if l.startswith("performance_model.instruction_count"): i=float(l.split("=")[1].split(",")[0])
            elif l.startswith("performance_model.cycle_count"): c=float(l.split("=")[1].split(",")[0])
            if i and c: break
    except Exception:
        return None
    return i/c if (i and c) else None

def load(results_dir, cfg):
    """Return {trace: IPC} for config `cfg` under results_dir/<cfg>_<trace>/simulation/sim.stats."""
    out={}
    for p in glob.glob(os.path.join(results_dir, f"{cfg}_*", "simulation", "sim.stats")):
        tr=p.split(os.sep+cfg+"_",1)[1].split(os.sep+"simulation")[0]
        v=ipc(p)
        if v: out[tr]=v
    return out

def gm(xs):
    return math.exp(sum(math.log(v) for v in xs)/len(xs)) if xs else float('nan')

def read_list(path):
    return [l.strip() for l in open(path) if l.strip() and not l.startswith("#")]

def speedup_vs(cfgd, based, traces):
    common=[t for t in traces if t in cfgd and t in based]
    return (gm([cfgd[t]/based[t] for t in common])-1)*100, len(common)
