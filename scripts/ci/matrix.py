import json,os
targets=[
 {"os":"windows","arch":"x64","runner":"windows-2022"},
 {"os":"windows","arch":"arm64","runner":"windows-11-arm"},
 {"os":"linux","arch":"x64","runner":"ubuntu-22.04"},
 {"os":"linux","arch":"arm64","runner":"ubuntu-22.04-arm"},
 {"os":"macos","arch":"x64","runner":"macos-15-intel"},
 {"os":"macos","arch":"arm64","runner":"macos-15"},
]
target=os.environ.get("BUILD_TARGET","all")
if target not in ("all","windows","linux","macos"):raise SystemExit("Invalid build target")
value=json.dumps({"include":[t for t in targets if target=="all" or t["os"]==target]},separators=(",",":"))
with open(os.environ["GITHUB_OUTPUT"],"a") as f:f.write("native="+value+"\n")
