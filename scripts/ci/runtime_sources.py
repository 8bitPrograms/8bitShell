#!/usr/bin/env python3
"""Keep license notices and exact corresponding source for bundled distro libraries."""
import json,shutil,subprocess,tarfile
from pathlib import Path
from distribution import ROOT,config,version,run
def collect(stage,out,cpu):
 stage=Path(stage);out=Path(out);records=[];packages={}
 for p in sorted((stage/"lib").glob("*")):
  if not p.is_file() or ".so" not in p.name or p.name.startswith("libQt"):continue
  # ldd discovers /lib paths whereas usrmerge metadata may list /usr/lib paths.
  paths=[]
  result=run(["ldconfig","-p"],capture_output=True,text=True)
  for line in result.stdout.splitlines():
   if line.strip().startswith(p.name+" "):paths.append(line.rsplit("=>",1)[-1].strip())
  owner=None
  for source in paths:
   for candidate in (source,str(Path(source).resolve()),source.replace("/usr/lib/","/lib/",1),source.replace("/lib/","/usr/lib/",1)):
    q=subprocess.run(["dpkg-query","-S",candidate],capture_output=True,text=True)
    if q.returncode==0:
     owner=q.stdout.splitlines()[0].rsplit(": ",1)[0];break
   if owner:break
  if not owner:raise ValueError(f"Cannot identify the source/license owner of bundled library {p.name}")
  fmt="${source:Package}\t${source:Version}\t${Version}"
  q=run(["dpkg-query","-W","-f",fmt,owner],capture_output=True,text=True)
  source_package,source_version,binary_version=q.stdout.split("\t")
  if not source_package:source_package=owner.split(":")[0]
  if not source_version:source_version=binary_version
  packages[source_package]=source_version
  record={"library":p.name,"binaryPackage":owner,"binaryVersion":binary_version,"sourcePackage":source_package,"sourceVersion":source_version}
  records.append(record)
  notice=Path("/usr/share/doc")/owner.split(":")[0]/"copyright"
  if not notice.exists():raise ValueError(f"Missing distribution license notice for {owner}")
  destination=stage/"share/runtime-licenses"/owner.replace(":","-")
  destination.mkdir(parents=True,exist_ok=True);shutil.copy2(notice,destination/"copyright")
 cache=ROOT/"tooling/runtime-sources"/cpu;cache.mkdir(parents=True,exist_ok=True)
 for package,v in packages.items():run(["apt-get","source","--download-only","--only-source",package+"="+v],cwd=cache)
 (cache/"inventory.json").write_text(json.dumps(records,indent=2)+"\n")
 (stage/"share/RUNTIME-SOURCES.json").write_text(json.dumps(records,indent=2)+"\n")
 asset=out/f'{config()["app"]}-{version()}-runtime-{cpu}-source.tar.gz'
 with tarfile.open(asset,"w:gz") as t:
  for p in cache.iterdir():
   if p.is_file():t.add(p,arcname=p.name)
 if asset.stat().st_size>=2_000_000_000:raise ValueError("Runtime source asset exceeds the per-file release limit")
