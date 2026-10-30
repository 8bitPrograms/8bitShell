"""Evaluates the draft-release job condition of build.yml for each trigger.

A tag push drafts. A workflow_dispatch drafts only with publish_draft, even on a
tag ref. Uses only the standard library, so it runs in the plan job."""
import re,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[3]
WORKFLOW=(ROOT/".github/workflows/build.yml").read_text()
DEFAULTS={k:v=="true" for k,v in re.findall(r"^      ([a-z0-9_]+):\n(?:        .*\n)*?        default: (true|false)$",WORKFLOW,re.M)}
def job_if(job):
 m=re.search(r"^  "+re.escape(job)+r":\n(?:    .*\n)*?    if: (.*)$",WORKFLOW,re.M)
 return m[1]
class Ctx(dict):
 def __getattr__(self,k):return self.get(k,"")
def evaluate(expr,event,ref,inputs,needs):
 # GitHub expressions to Python: names with dashes become item lookups, ! becomes not.
 py=re.sub(r"needs\.([A-Za-z0-9_-]+)\.result",lambda m:f"needs[{m[1]!r}]",expr)
 py=py.replace("&&"," and ").replace("||"," or ")
 py=re.sub(r"!(?!=)"," not ",py)
 ctx={"always":lambda:True,"startsWith":lambda a,b:str(a).lower().startswith(b.lower()),
  "github":Ctx(event_name=event,ref=ref),"inputs":Ctx(inputs),"needs":needs,"vars":Ctx()}
 return bool(eval(py,{"__builtins__":{}},ctx))
def drafts(event,ref,**given):
 inputs={**DEFAULTS,**given} if event=="workflow_dispatch" else {}
 needs={"plan":"success","native":"success","legacy-linux":"skipped","legacy-windows":"skipped"}
 return evaluate(job_if("draft-release"),event,ref,inputs,needs)
TAG="refs/tags/v1.0.0"
class DraftReleaseConditionTests(unittest.TestCase):
 def test_inputs_parsed(self):
  self.assertIn("publish_draft",DEFAULTS)
  self.assertFalse(DEFAULTS["publish_draft"])
 def test_tag_push_drafts(self):
  self.assertTrue(drafts("push",TAG))
 def test_dispatch_on_tag_needs_publish_draft(self):
  self.assertFalse(drafts("workflow_dispatch",TAG))
  self.assertFalse(drafts("workflow_dispatch",TAG,publish_draft=False))
  self.assertTrue(drafts("workflow_dispatch",TAG,publish_draft=True))
 def test_dry_run_never_drafts(self):
  if "finalize_dry_run" not in DEFAULTS:self.skipTest("no finalize_dry_run input")
  for ref in (TAG,"refs/heads/main"):
   for publish in (False,True):
    self.assertFalse(drafts("workflow_dispatch",ref,finalize_dry_run=True,publish_draft=publish))
 def test_branches_and_pull_requests_do_not_draft(self):
  self.assertFalse(drafts("push","refs/heads/main"))
  self.assertFalse(drafts("pull_request","refs/pull/1/merge"))
  self.assertFalse(drafts("workflow_dispatch","refs/heads/main"))
 def test_failed_builds_do_not_draft(self):
  needs={"plan":"success","native":"failure","legacy-linux":"skipped","legacy-windows":"skipped"}
  self.assertFalse(evaluate(job_if("draft-release"),"push",TAG,{},needs))
if __name__=="__main__":unittest.main()
