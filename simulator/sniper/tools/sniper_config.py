import collections

class DefaultValue:
  def __init__(self, value):
    self.val = value
  def __call__(self):
    return self.val

# Parse sim.cfg, read from file or from ic.job_output(jobid, 'sim.cfg'), into a dictionary
def parse_config(simcfg, cfg = None):
  import configparser, io
  cp = configparser.ConfigParser()
  cp.read_file(io.StringIO(str(simcfg)))
  if not cfg:
    cfg = {}
  for section in cp.sections():
    for key, value in sorted(cp.items(section)):
      # Remove comments at the end of a line
      value = value.split('#')[0]
      # Run through items sorted by key, so the default comes before the array one
      # Then cut off the [] array markers as they are only used to prevent duplicate option names which ConfigParser doesn't handle
      if key.endswith('[]'):
        key = key[:-2]
      key = '/'.join((section, key))
      if key in cfg:
        if type(cfg[key]) is not collections.defaultdict:
          # Make value heterogeneous (unless it already was, and we're parsing a second, override config file)
          defval = cfg[key]
          cfg[key] = collections.defaultdict(DefaultValue(defval))
        if ',' in value:
          for i, v in enumerate(value.split(',')):
            v = v.strip('"')
            if v: # Only fill in entries that have been provided
              cfg[key][i] = v
        else:
          print('Changing default',  cfg[key], end=' ')
          cfg[key].default_factory = DefaultValue(value.strip('"'))
          print(cfg[key])
      else: # If there has not been a default value provided, require all array data be populated
        if ',' in value:
          cfg[key] = []
          for i, v in enumerate(value.split(',')):
            cfg[key].append(v.strip('"'))
        else:
          cfg[key] = value.strip('"')
  return cfg


def get_config(config, key, index = None):
  is_hetero = (type(config[key]) == collections.defaultdict)
  if index is None:
    if is_hetero:
      return config[key].default_factory()
    else:
      return config[key]
  elif is_hetero:
    return config[key][index]
  else:
    return config[key]


def get_config_bool(config, key, index = None):
  value = get_config(config, key, index)
  if value.lower() in ('true', 'yes', '1'):
    return True
  elif value.lower() in ('false', 'no', '0'):
    return False
  else:
    raise ValueError('Invalid value for bool %s' % value)


def get_config_default(config, key, defaultval, index = None):
  if key in config:
    return get_config(config, key, index)
  else:
    return defaultval
