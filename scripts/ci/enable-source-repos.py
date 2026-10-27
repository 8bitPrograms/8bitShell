#!/usr/bin/env python3
"""Enable signed distribution source indices inside disposable Linux CI builders."""
from pathlib import Path
root=Path("/etc/apt")
for p in [root/"sources.list",*(root/"sources.list.d").glob("*.list")]:
 if not p.exists():continue
 lines=[line.replace("deb ","deb-src ",1) for line in p.read_text().splitlines() if line.startswith("deb ")]
 if lines:(root/"sources.list.d"/("8bit-"+p.stem+"-sources.list")).write_text("\n".join(lines)+"\n")
for p in list((root/"sources.list.d").glob("*.sources")):
 if p.name.startswith("8bit-"):continue
 text=p.read_text()
 if "Types: deb" in text:(root/"sources.list.d"/("8bit-"+p.name)).write_text(text.replace("Types: deb","Types: deb-src"))
