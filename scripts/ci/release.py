#!/usr/bin/env python3
"""Version mapping and explicit publication policy, retained for local tooling."""
import argparse,json,re
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
def version_info(value):
    m=re.fullmatch(r"(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)(?:-(alpha|beta|rc)\.([1-9]\d*))?",value)
    if not m:raise ValueError("Invalid semantic version")
    major,minor,patch=map(int,m.group(1,2,3));channel=m.group(4);counter=int(m.group(5) or 0)
    if major>255 or minor>255 or patch>64 or counter>99:raise ValueError("Version exceeds MSI mapping limits")
    slot=999 if not channel else {"alpha":100,"beta":300,"rc":600}[channel]+counter
    build=patch if major>=1 and not channel else patch*1000+slot
    return {"version":value,"core":f"{major}.{minor}.{patch}","msi":f"{major}.{minor}.{build}","unix":value.replace("-","~"),"channel":channel or "stable"}
def publication_errors(root=ROOT):
    root=Path(root);errors=[]
    path=root/"packaging/release-policy.json"
    try:policy=json.loads(path.read_text())
    except (OSError,ValueError):policy={}
    for key in ("projectLicenseApproved","thirdPartyRedistributionReviewed","windowsPackagingToolsReviewed"):
        if policy.get(key) is not True:errors.append(f"{key} has not been reviewed")
    license=policy.get("licenseExpression","")
    if not license or "Pending" in license:errors.append("Choose the project license")
    if not (root/"LICENSE").is_file():errors.append("Project LICENSE is missing")
    return errors
def main():
    p=argparse.ArgumentParser();p.add_argument("--release-check",action="store_true");p.add_argument("--version");a=p.parse_args()
    print(json.dumps(version_info(a.version or (ROOT/"PROJECT_VERSION").read_text().strip()),indent=2))
    if a.release_check:
        errors=publication_errors()
        if errors:raise SystemExit("Publication blocked:\n"+"\n".join(errors))
if __name__=="__main__":main()
