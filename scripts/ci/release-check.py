import json,os,sys
from pathlib import Path
from distribution import ROOT,config,version
c=config();v=version()
if os.environ.get("GITHUB_REF")!="refs/tags/v"+v:raise SystemExit("Draft releases require the exact matching version tag")
found=set()
for p in (ROOT/"dist/release").glob("manifest-*.json"):
 for a in json.loads(p.read_text()):found.add(a["os"]+"-"+a["arch"])
missing=set(c["platforms"]["stable"])-found
if missing:raise SystemExit("Missing tested target packages: "+", ".join(sorted(missing)))
if c.get("releasePolicy"):
 sys.path.insert(0,str(ROOT/"scripts/ci"))
 from release import publication_errors
 errors=publication_errors()
 if errors:raise SystemExit("Publication review remains incomplete:\n"+"\n".join(errors))
print("Complete target coverage; release policy satisfied")
