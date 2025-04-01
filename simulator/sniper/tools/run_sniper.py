import sys, os, time, subprocess, threading, tempfile, sniper_lib

def Tee(filename, prefix = ''):
  open(filename, 'w').close() # Make sure is writeable and empty
  obj = subprocess.Popen(['bash', '-c', 'while read line; do echo "%s"$line; echo $line >> %s; done' % (prefix, filename)], stdin = subprocess.PIPE)
  return obj.stdin.fileno()

def __run_program_redirect(app_id, program_func, program_arg, outputdir, run_id = 0):
  prefix_fd = Tee(os.path.join(outputdir, 'benchmark-app%d-run%d.log' % (app_id, run_id)), '[app%d] ' % app_id)
  os.dup2(prefix_fd, sys.stdout.fileno())
  os.dup2(prefix_fd, sys.stderr.fileno())
  program_func(program_arg)

def run_program_redirect(app_id, program_func, program_arg, outputdir, run_id = 0):
  import multiprocessing # module does not exist in Python <= 2.5, import only when needed
  proc = multiprocessing.Process(target = __run_program_redirect, args = (app_id, program_func, program_arg, outputdir, run_id))
  proc.start()
  proc.join()

def run_program_repeat(app_id, program_func, program_arg, outputdir):
  global running
  run_id = 0
  while running:
    print('[RUN-SNIPER] Starting application', app_id)
    run_program_redirect(app_id, program_func, program_arg, outputdir, run_id)
    print('[RUN-SNIPER] Application', app_id, 'done')
    time.sleep(1)
    run_id += 1

def run_multi(snipercmd, applications, repeat = False, outputdir = '.'):
  global running
  running = True
  p_sniper = subprocess.Popen([ 'bash', '-c', snipercmd ])

  threads = []
  for app in applications:
    t = threading.Thread(target = repeat and run_program_repeat or run_program_redirect,
                         args = (app['app_id'], app['func'], app['args'], outputdir))
    threads.append(t)

  for t in threads:
    t.start()
  p_sniper.wait()
  running = False # Simulator has ended, signal the benchmarks to stop restarting

  time.sleep(2)
  # Clean up benchmarks
  sniper_lib.kill_children()
  for t in threads:
    t.join()

  return p_sniper.returncode

# Determine libstdc++.so used by default by pin_sim.so (or sniper, if we aren't compiling pin_sim.so) using ldd
# Should take into account the current LD_LIBRARY_PATH
def get_cxx_inuse(sim_root, clear_ldlibpath = False):
  pin_sim = None
  for binary in ['%s/lib/pin_sim.so' % sim_root, '%s/lib/sniper' % sim_root]:
    if os.path.isfile(binary):
      pin_sim = binary
  if not pin_sim:
    return None
  try:
    ldd_out_name = tempfile.NamedTemporaryFile(delete = False).name
    ldlpsave = None
    if clear_ldlibpath:
      ldlpsave = os.environ.get('LD_LIBRARY_PATH', None)
      if ldlpsave:
        del os.environ['LD_LIBRARY_PATH']
    os.system('ldd %s > %s 2> /dev/null' % (pin_sim, ldd_out_name))
    if ldlpsave:
      os.environ['LD_LIBRARY_PATH'] = ldlpsave
    ldd_out = open(ldd_out_name).read()
    os.unlink(ldd_out_name)
    libcxx_path = os.path.dirname([ line.split()[2] for line in ldd_out.split('\n') if 'libstdc++.so.6' in line ][0])
  except Exception as e:
    print(repr(e), file=sys.stderr)
    return None
  return libcxx_path

# Find libstdc++.so version number in a given path
def get_cxx_version(path):
  filename = os.path.join(path, 'libstdc++.so.6')
  if os.path.exists(filename):
    realname = os.path.realpath(filename)
    try:
      version = int(realname.split('.')[-1])
      return version
    except Exception as e:
      print(repr(e), file=sys.stderr)
      return 0
  else:
    return 0

def get_cxx_override(sim_root, pin_home, arch):
  # Find which libstdc++.so is newer: either the system default one (with or without the LD_LIBRARY_PATH), or the Pin one
  cxx_versions = [get_cxx_inuse(sim_root), get_cxx_inuse(sim_root, clear_ldlibpath = True), '%s/%s/runtime/cpplibs' % (pin_home, arch)]
  if 'BENCHMARKS_ROOT' in os.environ:
    cxx_versions.append('%s/libs' % os.environ['BENCHMARKS_ROOT'])
  cxx_versions = [x for x in cxx_versions if x!=None]
  cxx_override = sorted([(get_cxx_version(x),x) for x in cxx_versions], key=lambda x:x[0])[-1][1]
  return cxx_override

# LD_LIBRARY_PATH setup
#
# There are many different versions of LD_LIBRARY_PATH to consider:
# - the initial LD_LIBRARY_PATH which will affect Python when running this script
# - the application being simulated (PIN_APP_LD_LIBRARY_PATH):
#   SNIPER_APP_LD_LIBRARY_PATH, defaults to original LD_LIBRARY_PATH
# - the Sniper pintool or standalone executable and Pin itself (PIN_VM_LD_LIBRARY_PATH):
#   Pin runtime libraries, system libstdc++ (depending on version),
#   can be extended by setting SNIPER_SIM_LD_LIBRARY_PATH
# - scripts being run inside the simulator (SNIPER_SCRIPT_LD_LIBRARY_PATH): original LD_LIBRARY_PATH
#   (e.g. mcpat when running powertrace.py)

def setup_env(sim_root, pin_home, arch, standalone = False, xed_home = None, torch_home = None):

  env = dict(os.environ)
  ld_library_path_orig = env.get('LD_LIBRARY_PATH', '')

  # Construct Sniper/Pintool LD_LIBRARY_PATH
  ld_library_path = []
  # Make sure that our version of Python is used, not the system version normally found in cxx_override
  ld_library_path.append('%s/python_kit/%s/lib' % (sim_root, arch))
  cxx_override = get_cxx_override(sim_root, pin_home, arch)
  ld_library_path.append(cxx_override)
  if not standalone:
    ld_library_path.append('%s/%s/runtime/cpplibs' % (pin_home, arch))
    ld_library_path.append('%s/%s/runtime' % (pin_home, arch))
    ld_library_path.append('%s/extras/xed-%s/lib' % (pin_home, arch))
  else:
    if xed_home:
      ld_library_path.append('%s/lib' % (xed_home,))
  if torch_home:
    ld_library_path.append('%s/lib' % (torch_home, ))
  if 'SNIPER_SIM_LD_LIBRARY_PATH' in os.environ:
    ld_library_path.append(os.environ['SNIPER_SIM_LD_LIBRARY_PATH'])
  env['LD_LIBRARY_PATH'] = ':'.join(ld_library_path)
  env['PIN_LD_RESTORE_REQUIRED'] = '1'
  env['PIN_VM_LD_LIBRARY_PATH'] = env['LD_LIBRARY_PATH'] # Pin VM and Pintool (Sniper) use LD_LIBRARY_PATH as modified above

  # Application LD_LIBRARY_PATH
  if 'SNIPER_APP_LD_LIBRARY_PATH' in env:
    env['PIN_APP_LD_LIBRARY_PATH'] = env['SNIPER_APP_LD_LIBRARY_PATH'] # Application uses explicit LD_LIBRARY_PATH
    del env['SNIPER_APP_LD_LIBRARY_PATH']
  else:
    env['PIN_APP_LD_LIBRARY_PATH'] = ld_library_path_orig  # Application uses original LD_LIBRARY_PATH

  # Scripts LD_LIBRARY_PATH
  env['SNIPER_SCRIPT_LD_LIBRARY_PATH'] = ld_library_path_orig  # Scripts running inside Sniper use original LD_LIBRARY_PATH

  # Other environment variables
  if 'SNIPER_APP_LD_PRELOAD' in env:
    env['PIN_APP_LD_PRELOAD'] = env['SNIPER_APP_LD_PRELOAD']
    del env['SNIPER_APP_LD_PRELOAD']
  elif 'LD_PRELOAD' in env:
    env['PIN_APP_LD_PRELOAD'] = env['LD_PRELOAD']
  env['LD_PRELOAD'] = ''
  env['PYTHONPATH'] = '%s/scripts:%s' % (sim_root, os.getenv('PYTHONPATH') or '')
  env['SNIPER_ROOT'] = sim_root
  env['GRAPHITE_ROOT'] = sim_root

  return env
