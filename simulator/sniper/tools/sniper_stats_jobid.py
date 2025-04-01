#comment next lines bellow to run script anyway
print("This script relies on Intel scripts to submit and collect sniper simualtions on a cluster, which not shipped with Sniper. Therefore, this script is not tested/updated for Python3 support", file=sys.stderr)
exit(0)

import sniper_stats, intelqueue, iqclient


class SniperStatsJobid(sniper_stats.SniperStatsBase):
  def __init__(self, jobid):
    self.jobid = jobid
    self.ic = iqclient.IntelClient()
    self.names = self.read_metricnames()

  def read_metricnames(self):
    return self.ic.graphite_dbresults(self.jobid, 'read_metricnames')

  def get_snapshots(self):
    return self.ic.graphite_dbresults(self.jobid, 'get_snapshots')

  def read_snapshot(self, prefix, metrics = None):
    return self.ic.graphite_dbresults(self.jobid, 'read_snapshot', {'prefix': prefix, 'metrics': metrics})

  def get_topology(self):
    return self.ic.graphite_dbresults(self.jobid, 'get_topology')

  def get_markers(self):
    return self.ic.graphite_dbresults(self.jobid, 'get_markers')

  def get_events(self):
    return self.ic.graphite_dbresults(self.jobid, 'get_events')
