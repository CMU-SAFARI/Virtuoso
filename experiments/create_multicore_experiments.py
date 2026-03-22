
import os
import argparse
import sys
from typing import List, Tuple, Dict, Any
from common.printutils import Colors, print_header


def load_yaml(yaml_path: str) -> Dict[str, Any]:
    try:
        import yaml  # type: ignore
    except Exception:
        print("Error: PyYAML not installed. Install via 'pip install pyyaml'.", file=sys.stderr)
        sys.exit(1)

    class UniqueKeyLoader(yaml.SafeLoader):
        pass

    def construct_mapping(loader, node, deep=False):
        seen = set()
        mapping = {}
        for key_node, value_node in node.value:
            key = loader.construct_object(key_node, deep=deep)
            if key in seen:
                raise yaml.constructor.ConstructorError(
                    "while constructing a mapping",
                    node.start_mark,
                    f"found duplicate key: {key}",
                    key_node.start_mark,
                )
            seen.add(key)
            mapping[key] = loader.construct_object(value_node, deep=deep)
        return mapping

    UniqueKeyLoader.construct_mapping = construct_mapping  # type: ignore

    try:
        with open(yaml_path, "r") as f:
            data = yaml.load(f, Loader=UniqueKeyLoader)
        if not isinstance(data, dict):
            raise ValueError("YAML root must be a mapping/dict")
        return data
    except FileNotFoundError:
        print(f"Error: YAML file not found: {yaml_path}", file=sys.stderr)
        sys.exit(1)
    except yaml.constructor.ConstructorError as e:
        print(f"YAML error: {e}", file=sys.stderr)
        sys.exit(1)
    except Exception as e:
        print(f"Error loading YAML: {e}", file=sys.stderr)
        sys.exit(1)


# ── Config resolution (reused from create_experiments.py) ────────────────────

def resolve_configs(
    yaml_data: Dict[str, Any],
    suite_config_keys: List[str],
    yaml_dir: str,
) -> List[Tuple[str, str]]:
    """
    Resolve configs from YAML into (variant_name, flags) tuples.
    Supports 'extends' and 'sweeps' just like the single-core version.
    """
    from itertools import product

    configs_root = yaml_data.get("configs", {})
    base_path_rel = configs_root.get("configs_base_path")
    if not base_path_rel:
        print("Error: 'configs_base_path' missing in YAML.", file=sys.stderr)
        sys.exit(1)

    configs_base_dir = os.path.normpath(os.path.join(yaml_dir, base_path_rel))
    resolved: List[Tuple[str, str]] = []
    seen_names: set = set()

    for cfg_key in suite_config_keys:
        cfg_entry = configs_root.get(cfg_key)
        if not cfg_entry:
            print(f"Error: Config key '{cfg_key}' not defined in YAML.", file=sys.stderr)
            sys.exit(1)

        base_name = cfg_entry.get("name")
        cfg_file = cfg_entry.get("file")
        if not base_name or not cfg_file:
            print(f"Error: Config '{cfg_key}' must include both 'name' and 'file'.", file=sys.stderr)
            sys.exit(1)

        cfg_path = os.path.join(configs_base_dir, cfg_file)
        base_flags = f" -c {cfg_path}"

        for ext in cfg_entry.get("extends", []):
            if isinstance(ext, str):
                base_flags += f" -g {ext}"
            elif isinstance(ext, list) and len(ext) == 2:
                _, opt = ext
                base_flags += f" -g {opt}"

        sweeps = cfg_entry.get("sweeps", [])
        if sweeps:
            dims = []
            for sw in sweeps:
                sid = sw.get("identifier")
                opt = sw.get("option")
                vals = sw.get("values")
                if isinstance(sid, str) and isinstance(opt, str):
                    dim_groups = [[(sid, opt, v)] for v in vals]
                    dims.append(dim_groups)
                elif isinstance(sid, list) and isinstance(opt, list):
                    num_entries = len(vals[0])
                    dim_groups = []
                    for i in range(num_entries):
                        group = [(sid[j], opt[j], vals[j][i]) for j in range(len(sid))]
                        dim_groups.append(group)
                    dims.append(dim_groups)

            for combo in product(*dims):
                assignments = [t for group in combo for t in group]
                suffix_parts = []
                flags = base_flags
                for sid_i, opt_i, val_i in assignments:
                    str_val = str(val_i).lower() if isinstance(val_i, bool) else str(val_i)
                    suffix_parts.append(f"-{sid_i}{str_val}")
                    flags += f" -g {opt_i}{str_val}"
                variant_name = base_name + "".join(suffix_parts)
                if variant_name in seen_names:
                    print(f"Error: Duplicate variant name '{variant_name}'", file=sys.stderr)
                    sys.exit(1)
                seen_names.add(variant_name)
                resolved.append((variant_name, flags))
        else:
            if base_name in seen_names:
                print(f"Error: Duplicate config name '{base_name}'", file=sys.stderr)
                sys.exit(1)
            seen_names.add(base_name)
            resolved.append((base_name, base_flags))

    return resolved


# ── Trace resolution ─────────────────────────────────────────────────────────

def _parse_tlist(tlist_path: str) -> List[List[Tuple[str, str, str, str]]]:
    """Parse a multicore tlist file.

    New format (semicolon-separated, per-trace base paths):
      # comment lines ignored
      base_path:name, filename, mpki, nlat; base_path:name, filename, mpki, nlat; ...

    Each non-comment, non-blank line is one N-core experiment mix.
    Returns a list of mixes, where each mix is a list of
    (name, full_path, mpki, nlat) tuples (one per core).
    """
    mixes: List[List[Tuple[str, str, str, str]]] = []
    try:
        with open(tlist_path, "r") as f:
            lines = [ln.strip() for ln in f]
        for line in lines:
            if not line or line.startswith("#"):
                continue
            # Split on semicolons to get individual trace entries
            trace_entries = [t.strip() for t in line.split(";") if t.strip()]
            mix = []
            for entry in trace_entries:
                # Format:  base_path:name, filename, mpki, nlat
                # The first colon separates base_path from the rest
                colon_idx = entry.find(":")
                if colon_idx == -1:
                    print(f"Warning: Skipping malformed trace entry (no colon): {entry}", file=sys.stderr)
                    continue
                base_path = entry[:colon_idx].strip()
                remainder = entry[colon_idx + 1:]
                parts = [p.strip() for p in remainder.split(",")]
                if len(parts) == 4:
                    name, filename, mpki, nlat = parts
                    full_path = os.path.join(base_path, filename)
                    mix.append((name, full_path, mpki, nlat))
                else:
                    print(f"Warning: Skipping malformed trace entry (expected 4 fields): {entry}", file=sys.stderr)
            if mix:
                mixes.append(mix)
    except FileNotFoundError:
        print(f"Error: tlist not found: {tlist_path}", file=sys.stderr)
    except Exception as e:
        print(f"Error reading tlist '{tlist_path}': {e}", file=sys.stderr)
    return mixes


def resolve_traces(
    yaml_data: Dict[str, Any],
    yaml_dir: str,
    trace_suite_name: str,
) -> List[List[Tuple[str, str, str, str]]]:
    """Resolve trace suite to list of mixes.
    Each mix is a list of (name, path, mpki, norm_lat) — one per core."""
    ts = yaml_data.get("trace_suite", {}).get(trace_suite_name)
    if not ts:
        print(f"Error: Trace suite '{trace_suite_name}' not defined.", file=sys.stderr)
        sys.exit(1)

    base_rel = ts.get("tracelist_base_path")
    tracelists = ts.get("tracelists", [])
    if not base_rel or not tracelists:
        print(f"Error: Trace suite '{trace_suite_name}' missing base path or tracelists.", file=sys.stderr)
        sys.exit(1)

    trace_list_dir = os.path.join(yaml_dir, base_rel)
    all_mixes: List[List[Tuple[str, str, str, str]]] = []
    for tl in tracelists:
        tl_path = os.path.join(trace_list_dir, tl)
        all_mixes.extend(_parse_tlist(tl_path))
    return all_mixes


# ── Utility ──────────────────────────────────────────────────────────────────

def sanitize_dirname(name: str) -> str:
    name = name.replace(",", "-").replace(" ", "-")
    for ch in [':', ';', '|', '\\', '/', '*', '?', '"', '<', '>', '!']:
        name = name.replace(ch, '-')
    while '--' in name:
        name = name.replace('--', '-')
    return name


def write_csv(path: str, header: List[str], rows) -> None:
    import csv
    with open(path, "w", newline='') as f:
        writer = csv.writer(f, quoting=csv.QUOTE_MINIMAL)
        writer.writerow(header)
        for row in rows:
            writer.writerow(row)


# ── Jobfile builder ──────────────────────────────────────────────────────────

def build_multicore_jobfile(
    jobfile_path: str,
    sniper_path: str,
    instruction_count: int,
    ncores: int,
    topology_overrides: List[str],
    configs: List[Tuple[str, str]],
    mixes: List[List[Tuple[str, str, str, str]]],
    output_root: str,
    sim_end: str = "last-restart",
) -> List[Tuple[str, str, str, str]]:
    """
    Build a jobfile for multicore experiments.

    Each job runs ONE config with ONE trace mix (one trace per core) in a
    single Sniper invocation:
      run-sniper -n <ncores> ... --traces=t0,t1,...,tN-1

    mixes: list of mixes, where each mix is a list of (name, path, mpki, nlat)
           with exactly ncores entries.

    Returns job metadata rows for the job_list CSV.
    """
    for mix_idx, mix in enumerate(mixes):
        if len(mix) != ncores:
            print(
                f"Error: Topology has {ncores} cores but mix #{mix_idx} has {len(mix)} traces. "
                f"They must match exactly.",
                file=sys.stderr,
            )
            sys.exit(1)

    jobs = []

    with open(jobfile_path, "w") as jf:
        jf.write("#!/bin/bash\n")

        counter = 0
        for mix_idx, mix in enumerate(mixes):
            # Build comma-separated trace arg
            trace_paths = [tp for _, tp, _, _ in mix]
            traces_arg = ",".join(trace_paths)

            # Build a short label from trace names  (e.g. "rnd+yankee_0060")
            trace_label = "+".join(sanitize_dirname(tn) for tn, _, _, _ in mix)
            if len(trace_label) > 120:
                trace_label = trace_label[:117] + "..."

            for cfg_name, cfg_flags in configs:
                safe_cfg = sanitize_dirname(cfg_name)
                out_dir_name = f"{safe_cfg}_{ncores}core_{trace_label}"
                # Truncate if too long for filesystem
                if len(out_dir_name) > 200:
                    out_dir_name = out_dir_name[:197] + "..."
                output_directory = os.path.join(output_root, out_dir_name)
                os.makedirs(output_directory, exist_ok=True)

                # Build command
                execution_command = os.path.join(sniper_path, "run-sniper")
                parts = [
                    execution_command,
                    f"-n {ncores}",
                    "--no-cache-warming",
                    "--genstats",
                    f"-s stop-by-icount-percore:{instruction_count}",
                    f"--sim-end={sim_end}",
                ]

                # Config flags (already include -c <path> -g ... from resolve_configs)
                parts.append(cfg_flags.strip())

                # Topology overrides
                for ov in topology_overrides:
                    parts.append(f"-g {ov}")

                # Static scheduler + host cores
                # Sniper trace mode spawns 2*N+2 threads: N SimThreads (gated
                # by the barrier), N TraceThreads (always running, reading
                # trace data), 1 Monitor, 1 main.  Request enough SLURM CPUs
                # so no threads are forced to share a physical core.
                host_cpus = 2 * ncores + 2
                parts.append("-g --scheduler/type=static")
                # num_host_cores controls the barrier release rate (how many
                # SimThreads run concurrently).  ncores is the right value —
                # there are only ncores simulated cores to release.
                parts.append(f"-g --general/num_host_cores={ncores}")

                # Output directory
                parts.append(f"-d {output_directory}")

                # Traces (must be last)
                parts.append(f"--traces={traces_arg}")

                command = " ".join(parts)

                # SLURM sbatch line
                sbatch_cmd = (
                    f"sbatch --exclude=kratos17"
                    f" -c {host_cpus}"
                    f" -J {safe_cfg}_{ncores}core_mix{mix_idx}"
                    f" --output={os.path.join(output_directory, 'slurm.out')}"
                    f" --error={os.path.join(output_directory, 'slurm.err')}"
                    f" {os.path.join(sniper_path, 'native_wrapper.sh')}"
                )

                jf.write(f'{sbatch_cmd} "{command}"\n')
                jobs.append((str(counter), safe_cfg, f"{ncores}core_mix{mix_idx}", output_directory))
                counter += 1

    return jobs


# ── Main ─────────────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(
        description="Create multicore experiment suite with SLURM jobfile",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  %(prog)s --artifact-path /home/kanellok/virtuoso_artifact --yaml clist_multicore.yaml --suite multicore_4core_homo --suite-dir-name mc_4core_homo_260220
  %(prog)s --artifact-path /home/kanellok/virtuoso_artifact --yaml clist_multicore.yaml --suite multicore_8core_hetero --force
  %(prog)s --artifact-path /home/kanellok/virtuoso_artifact --yaml clist_multicore.yaml --suite multicore_2core_homo multicore_4core_homo --suite-dir-name mc_scaling_homo
        """,
    )

    parser.add_argument("--artifact-path", type=str, required=True, help="Path to the virtuoso artifact root")
    parser.add_argument("--yaml", type=str, default="clist_multicore.yaml", help="Path to the multicore experiment YAML")
    parser.add_argument("--suite", type=str, nargs='+', required=True, help="Experiment suite name(s) from YAML")
    parser.add_argument("--suite-dir-name", type=str, default=None, help="Override directory name for the suite")
    parser.add_argument("--force", action="store_true", help="Allow reusing an existing suite directory")
    parser.add_argument("--no-color", action="store_true", help="Disable colored output")

    args = parser.parse_args()

    if args.no_color:
        Colors.disable()

    artifact_path = os.path.abspath(args.artifact_path)
    yaml_path = os.path.abspath(args.yaml)
    yaml_dir = os.path.dirname(yaml_path)

    print_header("Create Multicore Experiments Suite")
    data = load_yaml(yaml_path)

    suites_data = data.get("experiment_suites", {})

    # Validate all requested suites exist
    suite_names = args.suite
    for sn in suite_names:
        if sn not in suites_data:
            print(f"Error: Suite '{sn}' not found in YAML.", file=sys.stderr)
            print(f"Available suites: {', '.join(suites_data.keys())}", file=sys.stderr)
            sys.exit(1)

    # For multicore, each suite has its own topology + trace count.
    # When combining multiple suites, we generate separate jobs for each suite
    # but place them all in one jobfile/directory.

    dir_name = args.suite_dir_name or "_".join(suite_names)
    if not dir_name.startswith("exp_"):
        dir_name = f"exp_{dir_name}"

    suite_dir = os.path.join(artifact_path, "experiments", dir_name)
    if os.path.exists(suite_dir) and not args.force:
        print(f"Error: Suite directory already exists: {suite_dir}", file=sys.stderr)
        print("Use --force to reuse, or provide a unique --suite-dir-name.")
        sys.exit(1)
    elif os.path.exists(suite_dir) and args.force:
        print(f"Using existing suite directory due to --force: {suite_dir}")

    os.makedirs(suite_dir, exist_ok=True)
    results_dir = os.path.join(suite_dir, "results")
    os.makedirs(results_dir, exist_ok=True)

    sniper_path = os.path.join(artifact_path, "simulator", "sniper")
    topologies = data.get("topologies", {})

    all_jobs = []
    total_configs = 0
    total_traces = 0
    max_instruction_count = 0

    # We write one combined jobfile
    jobfile_path = os.path.join(suite_dir, "jobfile.sh")
    combined_jobfile_lines = ["#!/bin/bash\n"]

    for sn in suite_names:
        suite = suites_data[sn]
        topo_name = suite.get("topology")
        if topo_name not in topologies:
            print(f"Error: Topology '{topo_name}' not found in YAML.", file=sys.stderr)
            sys.exit(1)

        topo = topologies[topo_name]
        ncores = topo["ncores"]
        topo_overrides = topo.get("overrides", [])

        cfg_keys = suite.get("configs", [])
        trace_suite_name = suite.get("trace_suite")
        instruction_count = int(suite.get("instruction_count", 100_000_000))
        sim_end = suite.get("sim_end", "last-restart")
        max_instruction_count = max(max_instruction_count, instruction_count)

        # Resolve
        configs = resolve_configs(data, cfg_keys, yaml_dir)
        mixes = resolve_traces(data, yaml_dir, trace_suite_name)

        total_configs += len(configs)
        total_traces += sum(len(m) for m in mixes)

        # Per-suite sub-results dir
        suite_results = os.path.join(results_dir, sn)
        os.makedirs(suite_results, exist_ok=True)

        # Build a temporary jobfile, then merge into combined
        tmp_jobfile = os.path.join(suite_dir, f"_tmp_{sn}.sh")
        jobs = build_multicore_jobfile(
            tmp_jobfile, sniper_path, instruction_count,
            ncores, topo_overrides, configs, mixes, suite_results,
            sim_end=sim_end,
        )

        # Read the tmp jobfile (skip the shebang) and append
        with open(tmp_jobfile, "r") as f:
            lines = f.readlines()
        combined_jobfile_lines.append(f"\n# === Suite: {sn} ({ncores} cores) ===\n")
        combined_jobfile_lines.extend(lines[1:])  # skip #!/bin/bash
        os.remove(tmp_jobfile)

        # Offset job IDs
        for jid, cfg, topo_label, odir in jobs:
            all_jobs.append((str(len(all_jobs)), cfg, f"{sn}_{topo_label}", odir))

        # Write per-suite trace list (one row per core per mix)
        trace_csv = os.path.join(suite_dir, f"trace_list_{sn}.csv")
        trace_rows = []
        for mix_idx, mix in enumerate(mixes):
            for core_id, (tn, tp, mpki, nlat) in enumerate(mix):
                trace_rows.append((str(mix_idx), str(core_id), tn, tp, mpki, nlat))
        write_csv(trace_csv,
                  ["mix_id", "core_id", "trace_name", "trace_path", "L2TLB_MPKI", "normalized_ptw_latency"],
                  trace_rows)

    # Write combined jobfile
    with open(jobfile_path, "w") as f:
        f.writelines(combined_jobfile_lines)
    os.chmod(jobfile_path, 0o755)

    # Write combined job list
    job_list_path = os.path.join(suite_dir, "job_list.csv")
    write_csv(job_list_path, ["job_id", "config_name", "suite_topology", "output_dir"], all_jobs)

    # Write config list
    config_list_path = os.path.join(suite_dir, "config_list.csv")
    # Re-resolve to get full config listing
    all_cfg_keys = []
    for sn in suite_names:
        all_cfg_keys.extend(suites_data[sn].get("configs", []))
    all_cfg_keys = list(dict.fromkeys(all_cfg_keys))  # deduplicate preserving order
    all_configs = resolve_configs(data, all_cfg_keys, yaml_dir)
    write_csv(config_list_path, ["config_name", "config_flags"],
              [(cn, cf.strip()) for cn, cf in all_configs])

    print_header("Summary")
    print(f"  {Colors.BOLD}Suites:{Colors.RESET} {Colors.CYAN}{', '.join(suite_names)}{Colors.RESET}")
    print(f"  {Colors.BOLD}Suite dir:{Colors.RESET} {Colors.CYAN}{suite_dir}{Colors.RESET}")
    print(f"  {Colors.BOLD}Results dir:{Colors.RESET} {Colors.CYAN}{results_dir}{Colors.RESET}")
    print(f"  {Colors.BOLD}Max instructions:{Colors.RESET} {Colors.GREEN}{max_instruction_count:,}{Colors.RESET} per core")
    print(f"  {Colors.BOLD}Configs:{Colors.RESET} {Colors.GREEN}{len(all_configs)}{Colors.RESET} -> {config_list_path}")
    print(f"  {Colors.BOLD}Jobfile:{Colors.RESET} {jobfile_path}")
    print(f"  {Colors.BOLD}Jobs:{Colors.RESET} {Colors.GREEN}{len(all_jobs)}{Colors.RESET} -> {job_list_path}")

    for sn in suite_names:
        suite = suites_data[sn]
        topo = topologies[suite["topology"]]
        resolved_mixes = resolve_traces(data, yaml_dir, suite["trace_suite"])
        print(f"\n  {Colors.BOLD}[{sn}]{Colors.RESET}")
        print(f"    Topology: {suite['topology']} ({topo['ncores']} cores)")
        print(f"    Traces:   {suite['trace_suite']} ({len(resolved_mixes)} mix(es))")
        print(f"    Configs:  {suite['configs']}")
        print(f"    Icount:   {suite.get('instruction_count', 100000000):,} per core")


if __name__ == "__main__":
    main()
