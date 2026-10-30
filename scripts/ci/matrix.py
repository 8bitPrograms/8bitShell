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
# Pull requests share the org's five macOS runner slots. Keep those for main,
# where the Qt cache is actually saved. workflow_dispatch can still select macOS.
# TEMP(fix/main-ci-win-macos): revert before merge. Keep macOS on this PR to verify the fix.
if os.environ.get("GITHUB_EVENT_NAME")=="pull_request" and os.environ.get("GITHUB_HEAD_REF")!="fix/main-ci-win-macos":
    targets=[t for t in targets if t["os"]!="macos"]
value=json.dumps({"include":[t for t in targets if target=="all" or t["os"]==target]},separators=(",",":"))
with open(os.environ["GITHUB_OUTPUT"],"a") as f:f.write("native="+value+"\n")
