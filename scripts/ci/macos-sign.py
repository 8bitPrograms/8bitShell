#!/usr/bin/env python3
"""Optional GitHub-hosted macOS signing setup; no secrets are printed."""
import base64,os,secrets,subprocess,tempfile
from pathlib import Path
def private_run(args):
    result=subprocess.run(args,capture_output=True)
    if result.returncode:raise SystemExit(f"macOS signing setup failed (exit {result.returncode})")
def main():
    encoded=os.environ.get("MACOS_CERTIFICATE_P12","")
    if not encoded:
        print("Signing certificate not configured; Actions candidates use ad-hoc signing.")
        return
    password=os.environ.get("MACOS_CERTIFICATE_PASSWORD","")
    identity=os.environ.get("MACOS_SIGN_IDENTITY","")
    if not identity:raise SystemExit("MACOS_SIGN_IDENTITY is required with a certificate")
    folder=Path(tempfile.mkdtemp(prefix="8bit-signing-"));cert=folder/"certificate.p12";cert.write_bytes(base64.b64decode(encoded,validate=True));cert.chmod(0o600)
    keychain=folder/"build.keychain-db";keypass=secrets.token_urlsafe(32)
    private_run(["security","create-keychain","-p",keypass,str(keychain)])
    private_run(["security","set-keychain-settings","-lut","21600",str(keychain)])
    private_run(["security","unlock-keychain","-p",keypass,str(keychain)])
    private_run(["security","import",str(cert),"-k",str(keychain),"-P",password,"-T","/usr/bin/codesign","-T","/usr/bin/security"])
    private_run(["security","set-key-partition-list","-S","apple-tool:,apple:","-s","-k",keypass,str(keychain)])
    existing=subprocess.run(["security","list-keychains","-d","user"],capture_output=True,text=True,check=True).stdout
    import shlex
    private_run(["security","list-keychains","-d","user","-s",str(keychain),*shlex.split(existing)])
    cert.unlink()
    with open(os.environ["GITHUB_ENV"],"a") as f:
        f.write("MACOS_SIGN_IDENTITY="+identity+"\nMACOS_TEMP_KEYCHAIN="+str(keychain)+"\n")
    print("Temporary signing identity prepared")
if __name__=="__main__":main()
