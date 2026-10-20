#!/usr/bin/env python3
"""Fails if the add-on imports MSVCP140 functions that Archicad 23 (3003) modules do not use.

Older VC++ runtimes present on Archicad 23 machines lack some newer exports (e.g. the
std::mutex helpers _Mtx_init_in_situ ...); an unresolved import makes Windows refuse to
load the add-on ("Tento doplnek nelze otevrit").
"""
import os
import sys

import pefile

root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
apx = sys.argv[1]
allow = set()
with open(os.path.join(root, "tools", "msvcp140_allowlist.txt"), encoding="utf-8") as f:
    for line in f:
        line = line.strip()
        if line and not line.startswith("#"):
            allow.add(line)

pe = pefile.PE(apx)
bad = []
count = 0
for entry in pe.DIRECTORY_ENTRY_IMPORT:
    if entry.dll.decode().upper() == "MSVCP140.DLL":
        for imp in entry.imports:
            if imp.name:
                count += 1
                name = imp.name.decode()
                if name not in allow:
                    bad.append(name)
for b in bad:
    print(f"::error::MSVCP140 import not available in Archicad 23 runtime: {b}")
print(f"::notice::MSVCP140 imports checked: {count}, problems: {len(bad)}")
sys.exit(1 if bad else 0)
