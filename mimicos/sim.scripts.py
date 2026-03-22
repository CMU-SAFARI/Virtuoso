
import sys
from importlib import util
def load_file_as_module(name, location):
    sys.path.insert(0,location.rsplit('/', 1)[0])
    spec = util.spec_from_file_location(name, location)
    module = util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module
import os as _os
_sniper_root = _os.environ.get("SNIPER_ROOT", _os.path.join(_os.path.dirname(_os.path.abspath(__file__)), ".."))
sys.argv = [ _os.path.join(_sniper_root, "scripts/stop-by-icount.py"), "10000000" ]
load_file_as_module("stop-by-icount", _os.path.join(_sniper_root, "scripts/stop-by-icount.py"))

