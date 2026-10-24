#!/usr/bin/env python3
"""Record how waf links every shared module (objects, internal archives, deps).

Deletes the built .so files, relinks them with `waf build -v`, and parses the
printed link commands into JSON for ps5/staticlink.py.
"""
import ast, json, os, re, subprocess, sys
from pathlib import Path

engine = Path(os.environ["SRC"]) / "source-engine"
build = engine / "build"
for so in build.rglob("*.so"):
    so.unlink()
env = dict(os.environ, CC=os.environ["PS5_CC"], CXX=os.environ["PS5_CXX"], AR=os.environ["PS5_AR"])
proc = subprocess.run([sys.executable, "waf", "build", "-v", "-j1"], cwd=engine, env=env,
                      capture_output=True, text=True)
log = proc.stdout + proc.stderr
(build / "ps5-link.log").write_text(log)

links = {}
for line in log.splitlines():
    if " runner [" not in line or "'-shared'" not in line:
        continue
    argv = ast.literal_eval(line[line.index("["):])
    out = next(a[2:] for a in argv if a.startswith("-o"))
    mod = re.sub(r"^lib", "", Path(out).name)[:-3]
    libpaths = [build / a[2:] for a in argv if a.startswith("-L")]
    objects, archives, shared, external = [], [], [], []
    static = False
    for a in argv[2:]:
        if a == "-Wl,-Bstatic":
            static = True
        elif a == "-Wl,-Bdynamic":
            static = False
        elif a.endswith(".o"):
            objects.append(str((build / a).resolve()))
        elif a.endswith(".a"):
            archives.append(str((build / a).resolve()))
        elif a.startswith("-l"):
            lib = a[2:]
            hit = next((d / f"lib{lib}.a" for d in libpaths
                        if str(d).startswith(str(build)) and (d / f"lib{lib}.a").exists()), None)
            if static and hit:
                archives.append(str(hit.resolve()))
            elif any(str(d).startswith(str(build)) and d.name == lib or (d / f"lib{lib}.so").exists()
                     for d in libpaths if str(d).startswith(str(build))):
                shared.append(lib)
            else:
                external.append(lib)
    links[mod] = {"output": out, "objects": objects, "archives": archives,
                  "shared": shared, "external": external}
    print(f"{mod:24s} objects={len(objects):4d} archives={len(archives):2d} shared={shared} external={external}")
Path(sys.argv[1] if len(sys.argv) > 1 else build / "ps5-links.json").write_text(json.dumps(links, indent=1))
