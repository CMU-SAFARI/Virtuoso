# End ROI after each core has simulated at least X instructions.
# Traces that finish early are restarted (use with --sim-end=last-restart).
#
# Usage: -s stop-by-icount-percore:10000000          # Each core simulates at least 10M instructions
#        -s stop-by-icount-percore:10000000 --roi     # Wait for ROI, then each core does 10M
#
# The script checks per-core instruction counts every callback.
# It ends simulation only when ALL cores have reached the target.

import sys
import sim


class StopByIcountPerCore:

  def _min_callback(self):
    return min(self.ninstrs_start if self.ninstrs_start else float('inf'), self.ninstrs, self.min_ins_global)


  def setup(self, args):
    self.magic = sim.config.get_bool('general/magic')
    self.min_ins_global = int(sim.config.get('core/hook_periodic_ins/ins_global'))
    self.ncores = sim.config.ncores
    self.wait_for_app_roi = False
    self.verbose = True

    args = dict(enumerate((args or '').split(':')))
    self.ninstrs = int(args.get(0, 1e9))    # per-core target
    start = args.get(1, None)

    output_dir = sim.config.get('general/output_dir')
    with open(output_dir + '/heartbeat.txt', 'w') as f:
      f.write("Heartbeats written here (per-core icount mode)\n")

    if start == '':
      start = None
    roiscript = sim.config.get_bool('general/roi_script')

    # Track per-core starting icount (recorded when ROI begins)
    self.core_icount_start = [0] * self.ncores
    self.core_done = [False] * self.ncores

    if start is None and not roiscript:
      if self.magic:
        self.roi_rel = True
        self.ninstrs_start = 0
        self.inroi = False
        print('[PERCORE-ICOUNT] Waiting for application ROI (%d cores, %d instrs/core)' % (self.ncores, self.ninstrs))
      else:
        self.roi_rel = True
        self.ninstrs_start = 0
        self.inroi = True
        print('[PERCORE-ICOUNT] Starting in ROI (%d cores, %d instrs/core)' % (self.ncores, self.ninstrs))
    else:
      if self.magic:
        print('[PERCORE-ICOUNT] ERROR: Application ROIs and warmup cannot be combined with --roi')
        sim.control.abort()
        self.done = True
        return
      if not roiscript:
        print('[PERCORE-ICOUNT] ERROR: --roi-script required when using start instruction count')
        sim.control.abort()
        self.done = True
        return
      if start is None:
        start = self.min_ins_global
      if str(start).startswith('roi+'):
        self.ninstrs_start = int(start[4:])
        self.roi_rel = True
        self.wait_for_app_roi = True
        print('[PERCORE-ICOUNT] Starting %d instructions after ROI begin' % self.ninstrs_start)
      else:
        self.ninstrs_start = int(start)
        self.roi_rel = False
        print('[PERCORE-ICOUNT] Starting after %d aggregate instructions' % self.ninstrs_start)
      self.inroi = False

    print('[PERCORE-ICOUNT] Target: each core simulates at least %d instructions' % self.ninstrs)
    self.done = False
    sim.util.EveryIns(self._min_callback(), self.periodic, roi_only=(start is None))


  def _get_per_core_icounts(self):
    """Get current instruction count for each core."""
    counts = []
    for core in range(self.ncores):
      try:
        count = sim.stats.get('performance_model', core, 'instruction_count')
      except:
        try:
          count = sim.stats.get('core', core, 'instructions')
        except:
          count = 0
      counts.append(count)
    return counts


  def _get_per_core_clocks_ns(self):
    """Get current local clock (elapsed_time) for each core in nanoseconds."""
    clocks = []
    for core in range(self.ncores):
      try:
        fs = sim.stats.get('performance_model', core, 'elapsed_time')
        clocks.append(fs // 1000000)  # femtoseconds -> nanoseconds
      except:
        clocks.append(0)
    return clocks


  def hook_application_roi_begin(self):
    if self.wait_for_app_roi:
      print('[PERCORE-ICOUNT] Application at ROI begin, fast-forwarding for %d more instructions' % self.ninstrs_start)
      self.wait_for_app_roi = False
      self.ninstrs_start = sim.stats.icount() + self.ninstrs_start


  def hook_roi_begin(self):
    if self.magic:
      self.ninstrs_start = sim.stats.icount()
      self.inroi = True
      # Record per-core starting icounts
      self.core_icount_start = self._get_per_core_icounts()
      print('[PERCORE-ICOUNT] ROI started, each core will simulate %d instructions' % self.ninstrs)

    output_dir = sim.config.get('general/output_dir')
    with open(output_dir + '/heartbeat.txt', 'a') as f:
      f.write("ROI started, target %d instrs/core\n" % self.ninstrs)


  def periodic(self, icount, icount_delta):
    if self.done:
      return

    per_core = self._get_per_core_icounts()

    if self.verbose:
      min_core_instrs = min(per_core) if per_core else 0
      max_core_instrs = max(per_core) if per_core else 0
      clocks = self._get_per_core_clocks_ns()
      global_time_ns = sim.stats.time() // 1000000  # fs -> ns
      min_clock = min(clocks) if clocks else 0
      max_clock = max(clocks) if clocks else 0
      print('[PERCORE-ICOUNT] Aggregate: %d  Min-core: %d  Max-core: %d  Target/core: %d  global_time: %dns  clocks: [%d..%d]ns' %
            (icount, min_core_instrs, max_core_instrs, self.ninstrs, global_time_ns, min_clock, max_clock))

      output_dir = sim.config.get('general/output_dir')
      global_time_ns = sim.stats.time() // 1000000  # fs -> ns
      with open(output_dir + '/heartbeat.txt', 'a') as f:
        detail = ', '.join(['c%d=%d' % (c, per_core[c]) for c in range(self.ncores)])
        clock_detail = ', '.join(['c%d=%dns' % (c, clocks[c]) for c in range(self.ncores)])
        f.write('[PERCORE-ICOUNT] agg=%d  global_time=%dns  [%s]  target=%d/core\n' % (icount, global_time_ns, detail, self.ninstrs))
        f.write('[PERCORE-CLOCK]  [%s]\n' % clock_detail)

    # Handle starting ROI based on aggregate icount
    if not self.inroi and not self.wait_for_app_roi and icount > self.ninstrs_start:
      print('[PERCORE-ICOUNT] Starting ROI after %d aggregate instructions' % icount)
      sim.control.set_roi(True)
      self.inroi = True
      # Record per-core starting icounts
      self.core_icount_start = self._get_per_core_icounts()
      return

    if not self.inroi:
      return

    # Check per-core: has each core simulated at least ninstrs since ROI start?
    all_done = True
    for core in range(self.ncores):
      core_simulated = per_core[core] - self.core_icount_start[core]
      if core_simulated < self.ninstrs:
        all_done = False
      elif not self.core_done[core]:
        self.core_done[core] = True
        print('[PERCORE-ICOUNT] Core %d reached target (%d instrs simulated)' % (core, core_simulated))

    if all_done:
      print('[PERCORE-ICOUNT] All %d cores reached %d instructions. Ending ROI.' % (self.ncores, self.ninstrs))
      for core in range(self.ncores):
        core_simulated = per_core[core] - self.core_icount_start[core]
        print('[PERCORE-ICOUNT]   Core %d: %d instructions' % (core, core_simulated))
      sim.control.set_roi(False)
      self.inroi = False
      self.done = True
      sim.control.abort()

    # Progress tracking
    if not self.done:
      min_progress = 0
      for core in range(self.ncores):
        core_simulated = per_core[core] - self.core_icount_start[core]
        p = core_simulated / float(self.ninstrs + 1)
        if core == 0 or p < min_progress:
          min_progress = p
      sim.control.set_progress(min(min_progress, 0.99))


sim.util.register(StopByIcountPerCore())
