import sys
import sysconfig
import os
import json

def gather():
    inc = sysconfig.get_path("include")
    data = {
        "executable": sys.executable,
        "version": sys.version,
        "prefix": sys.prefix,
        "base_prefix": getattr(sys, "base_prefix", None),
        "include": inc,
        "include_exists": os.path.isdir(inc) if inc else False,
        "python_h_exists": os.path.exists(os.path.join(inc, "Python.h")) if inc else False,
        "paths": sysconfig.get_paths()
    }
    print(json.dumps(data))

if __name__ == "__main__":
    gather()