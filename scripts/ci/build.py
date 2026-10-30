#!/usr/bin/env python3
"""Native build, tests, deployment, clean-environment smoke test, then packages."""
import argparse,json,os,platform,shutil,subprocess,sys,tarfile,tempfile
from pathlib import Path
from distribution import ROOT,config,version,arch,run as raw_run,download,extract,bundle_linux,launcher,archive_tree,linux_packages,stamp_assets,verify_binary
def _hosted_error(text):
    lines=[line.strip() for line in (text or "").splitlines() if line.strip()]
    drop=lambda line: "ICE91" in line or "LGHT1076" in line
    primary_keys=("FAIL!","QFAIL","Actual","Expected","Bad configuration","error LGHT","CMake Error","ValueError","Could not find the Qt","This application failed")
    summary_keys=("The following tests FAILED","MSI link","***Failed")
    primary=[line for line in lines if (any(key in line for key in primary_keys) or "error C" in line or "error LNK" in line) and not drop(line)]
    summary=[line for line in lines if any(key in line for key in summary_keys) and not drop(line)]
    chosen=(primary[:12]+primary[-4:]+summary[-4:]) or lines[-12:]
    message=" | ".join(chosen or ["command failed"]).replace("%","%25").replace("\r","%0D").replace("\n","%0A")
    print(f"::error::{message[-3500:]}",flush=True)
def run(args,**kw):
    # Hosted logs are not readable without admin. Keep the failing command tail in one annotation.
    if any(key in kw for key in ("stdout","stderr","capture_output")):return raw_run(args,**kw)
    kw=dict(kw);kw.pop("check",None);command=[str(a) for a in args]
    log=ROOT/"build"/"package-step.log";log.parent.mkdir(parents=True,exist_ok=True)
    try:proc=subprocess.run(command,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,encoding="utf-8",errors="replace",**kw)
    except subprocess.TimeoutExpired as exc:
        output=exc.stdout.decode("utf-8","replace") if isinstance(exc.stdout,bytes) else (exc.stdout or "")
        with log.open("a",encoding="utf-8",errors="replace") as handle:handle.write(f"\n$ {' '.join(command)}\n{output}\n")
        _hosted_error(output or str(exc));raise
    output=proc.stdout or "";sys.stdout.write(output);sys.stdout.flush()
    with log.open("a",encoding="utf-8",errors="replace") as handle:handle.write(f"\n$ {' '.join(command)}\n{output}")
    if proc.returncode:_hosted_error(output or f"exit {proc.returncode}");raise subprocess.CalledProcessError(proc.returncode,command)
    return proc
def openssh_windows(stage,cpu,build_layout=False):
    lock=json.loads((ROOT/"packaging/openssh.lock.json").read_text())
    item=lock["assets"][cpu];cache=ROOT/"tooling/openssh";archive=download(item["url"],cache/item["name"],item["sha256"])
    unpack=cache/cpu
    if not unpack.exists():extract(archive,unpack)
    matches=list(unpack.rglob("ssh.exe"))
    if len(matches)!=1:raise ValueError("Unexpected OpenSSH package layout")
    dest=stage/("openssh" if build_layout else "bin/openssh");dest.mkdir(parents=True,exist_ok=True)
    for p in matches[0].parent.iterdir():
        if p.is_file() and (p.name in ("ssh.exe","sftp.exe","scp.exe","ssh-keygen.exe","ssh-sk-helper.exe","ssh-pkcs11-helper.exe") or p.suffix.lower()==".dll" or p.name.lower().startswith(("license","licence","notice"))):shutil.copy2(p,dest/p.name)
def crt_windows(stage,cpu):
    vs=os.environ.get("VSINSTALLDIR")
    if not vs:raise ValueError("Initialize the native MSVC environment first")
    roots=sorted((Path(vs)/"VC/Redist/MSVC").glob("*/"+cpu+"/Microsoft.VC*.CRT"),reverse=True)
    if not roots:raise ValueError("The redistributable MSVC CRT is missing from the build toolchain")
    for p in roots[0].glob("*.dll"):shutil.copy2(p,stage/"bin"/p.name)
    for p in (Path(vs)/"Licenses").glob("*"):
        if p.is_file() and "redist" in p.name.lower():shutil.copy2(p,stage/"share"/p.name)
def _windows_unelevated(args,env,timeout,workdir,readable,log_path):
    # GitHub-hosted Windows runners log in as an elevated admin, and the app refuses
    # to start elevated. A Safer/Medium-IL copy of the admin token still reports
    # TokenElevation, so run the smoke test as a throwaway standard local user
    # instead: its token is never elevated. Windows only.
    import ctypes,msvcrt,secrets
    from ctypes import wintypes as w
    k32=ctypes.WinDLL("kernel32",use_last_error=True);adv=ctypes.WinDLL("advapi32",use_last_error=True)
    command=[str(a) for a in args]
    def fail(what):
        code=ctypes.get_last_error()
        raise OSError(f"Non-elevated smoke launch failed at {what}: {ctypes.FormatError(code).strip()} (Windows error {code})")
    def call(fn,what,*fargs):
        if not fn(*fargs):fail(what)
    class STARTUPINFOW(ctypes.Structure):
        _fields_=[("cb",w.DWORD),("lpReserved",w.LPWSTR),("lpDesktop",w.LPWSTR),("lpTitle",w.LPWSTR),("dwX",w.DWORD),("dwY",w.DWORD),
                  ("dwXSize",w.DWORD),("dwYSize",w.DWORD),("dwXCountChars",w.DWORD),("dwYCountChars",w.DWORD),("dwFillAttribute",w.DWORD),
                  ("dwFlags",w.DWORD),("wShowWindow",w.WORD),("cbReserved2",w.WORD),("lpReserved2",ctypes.c_void_p),
                  ("hStdInput",w.HANDLE),("hStdOutput",w.HANDLE),("hStdError",w.HANDLE)]
    class PROCESS_INFORMATION(ctypes.Structure):_fields_=[("hProcess",w.HANDLE),("hThread",w.HANDLE),("dwProcessId",w.DWORD),("dwThreadId",w.DWORD)]
    adv.CreateProcessWithLogonW.argtypes=[w.LPCWSTR,w.LPCWSTR,w.LPCWSTR,w.DWORD,w.LPCWSTR,w.LPWSTR,w.DWORD,ctypes.c_void_p,w.LPCWSTR,ctypes.POINTER(STARTUPINFOW),ctypes.POINTER(PROCESS_INFORMATION)];adv.CreateProcessWithLogonW.restype=w.BOOL
    k32.CloseHandle.argtypes=[w.HANDLE];k32.CloseHandle.restype=w.BOOL
    k32.WaitForSingleObject.argtypes=[w.HANDLE,w.DWORD];k32.WaitForSingleObject.restype=w.DWORD
    k32.GetExitCodeProcess.argtypes=[w.HANDLE,ctypes.POINTER(w.DWORD)];k32.GetExitCodeProcess.restype=w.BOOL
    k32.TerminateProcess.argtypes=[w.HANDLE,w.UINT];k32.TerminateProcess.restype=w.BOOL
    LOGON_WITH_PROFILE,STARTF_USESTDHANDLES,CREATE_UNICODE_ENVIRONMENT,CREATE_NO_WINDOW=0x1,0x100,0x400,0x08000000
    WAIT_OBJECT_0,WAIT_TIMEOUT,STATUS_DLL_INIT_FAILED=0,0x102,0xC0000142
    info=PROCESS_INFORMATION();quiet=dict(stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    user="b8smoke";password=secrets.token_urlsafe(24)+"aA1!"
    print(f"::add-mask::{password}",flush=True)
    subprocess.run(["net","user",user,"/delete"],**quiet)  # left over from an earlier attempt
    subprocess.run(["net","user",user,password,"/add","/y"],check=True,**quiet)
    try:
        for path,rights in ((workdir,"F"),(readable,"RX")):
            subprocess.run(["icacls",str(path),"/grant",f"{user}:(OI)(CI){rights}","/T","/C","/Q"],check=True,stdout=subprocess.DEVNULL)
        env=dict(env)
        for key,sub in (("TEMP","tmp"),("TMP","tmp"),("APPDATA","appdata"),("LOCALAPPDATA","localappdata")):
            target=workdir/"smoke-user"/sub;target.mkdir(parents=True,exist_ok=True);env[key]=str(target)
        block=ctypes.create_unicode_buffer("".join(f"{k}={v}\0" for k,v in sorted(env.items(),key=lambda kv:kv[0].upper()))+"\0")
        cmdline=ctypes.create_unicode_buffer(subprocess.list2cmdline(command))
        with open(log_path,"wb") as output:
            handle=msvcrt.get_osfhandle(output.fileno());os.set_handle_inheritable(handle,True)
            # lpDesktop stays NULL: CreateProcessWithLogonW then grants the logon user
            # access to the inherited window station and desktop.
            startup=STARTUPINFOW();startup.cb=ctypes.sizeof(startup);startup.dwFlags=STARTF_USESTDHANDLES
            startup.hStdOutput=startup.hStdError=handle
            flags=CREATE_UNICODE_ENVIRONMENT|CREATE_NO_WINDOW
            if not adv.CreateProcessWithLogonW(user,".",password,LOGON_WITH_PROFILE,command[0],cmdline,flags,block,str(workdir),ctypes.byref(startup),ctypes.byref(info)):
                fail("CreateProcessWithLogonW")
            waited=k32.WaitForSingleObject(info.hProcess,int(timeout*1000))
            if waited==WAIT_TIMEOUT:
                k32.TerminateProcess(info.hProcess,1);k32.WaitForSingleObject(info.hProcess,10000)
                raise subprocess.TimeoutExpired(command,timeout)
            if waited!=WAIT_OBJECT_0:fail("WaitForSingleObject")
            code=w.DWORD();call(k32.GetExitCodeProcess,"GetExitCodeProcess",info.hProcess,ctypes.byref(code))
        if code.value==STATUS_DLL_INIT_FAILED:
            _hosted_error(f"Smoke run exited with STATUS_DLL_INIT_FAILED (0xC0000142): the {user} smoke user likely lacks window-station or desktop access")
        return code.value
    finally:
        for h in (info.hThread,info.hProcess):
            if h:k32.CloseHandle(h)
        # Files the smoke user created (its profile dir) are readable only by that user;
        # hand workdir back to the runner before deleting it so diagnostics can upload.
        # Best effort: a cleanup failure must not mask the smoke result.
        for reclaim in (["takeown","/F",str(workdir),"/R","/D","Y"],["icacls",str(workdir),"/reset","/T","/C","/Q"]):
            try:
                if subprocess.run(reclaim,**quiet).returncode:print(f"::warning::{reclaim[0]} on {workdir} failed; diagnostics upload may be incomplete",flush=True)
            except Exception as exc:print(f"::warning::{reclaim[0]} on {workdir} failed: {exc}",flush=True)
        try:subprocess.run(["net","user",user,"/delete"],**quiet)
        except Exception:pass
def smoke_run(args,env,timeout,workdir,readable):
    if platform.system()!="Windows":return run(args,env=env,timeout=timeout)
    command=[str(a) for a in args];log=workdir/"smoke-output.log"
    try:code=_windows_unelevated(command,env,timeout,workdir,readable,log)
    except subprocess.TimeoutExpired:
        _hosted_error(f"Smoke test timed out after {timeout}s: {' '.join(command)}");raise
    finally:
        output=log.read_text("utf-8","replace") if log.exists() else ""
        sys.stdout.write(output);sys.stdout.flush()
        with (ROOT/"build"/"package-step.log").open("a",encoding="utf-8",errors="replace") as handle:handle.write(f"\n$ (non-elevated) {' '.join(command)}\n{output}")
    if code:
        _hosted_error(output or f"exit {code:#x}")
        raise subprocess.CalledProcessError(code,command,output=f"Non-elevated smoke run exited with {code} ({code:#010x})")
def smoke(stage,system):
    c=config();app=c["app"];out=ROOT/"build"/("package-smoke-"+system+"-"+arch());out.mkdir(parents=True,exist_ok=True)
    runtime=Path(tempfile.mkdtemp(prefix="run-",dir=out));temporary=runtime/"temp";temporary.mkdir()
    env=os.environ.copy();env.update(TEMP=str(temporary),TMP=str(temporary),TMPDIR=str(temporary))
    for k in list(env):
        if k in ("LD_LIBRARY_PATH","DYLD_LIBRARY_PATH","QT_PLUGIN_PATH","QML2_IMPORT_PATH","QML_IMPORT_PATH"):env.pop(k)
    env.update(QT_QPA_PLATFORM="offscreen",QT_QUICK_BACKEND="software")
    env[c["environment"]+"_PROFILE"]=str(runtime/"profile")
    if system=="windows":
        exe=stage/"bin"/(app+".exe");env["PATH"]=str(Path(os.environ["SystemRoot"])/"System32");env.pop("PSModulePath",None)
        env["QT_QPA_FONTDIR"]=str(Path(os.environ["SystemRoot"])/"Fonts")
    elif system=="macos":exe=stage/(app+".app")/"Contents/MacOS"/app
    else:exe=stage/app;env["PATH"]="/usr/bin:/bin"
    args=[exe,"--smoke-test",out] if app=="8bitPad" else [exe,"--screenshot",out/"shell.png","--window-size","1280x800"]
    smoke_run(args,env,600,out,stage)
    if app=="8bitShell":
        helper=(stage/"bin" if system=="linux" else exe.parent)/("8bitShell-askpass.exe" if system=="windows" else "8bitShell-askpass")
        for key in ("BIT8SHELL_ASKPASS_SOCKET","BIT8SHELL_ASKPASS_TOKEN","SSH_ASKPASS_PROMPT"):
            env.pop(key,None)
        result=subprocess.run([helper],env=env,capture_output=True,timeout=10)
        if result.returncode!=1 or result.stdout:
            raise RuntimeError("Packaged askpass helper did not reject an unauthenticated invocation cleanly")
def main():
    (ROOT/"build").mkdir(exist_ok=True);(ROOT/"build"/"package-step.log").write_text("")
    p=argparse.ArgumentParser();p.add_argument("--qt-root",required=True);p.add_argument("--jobs",type=int,default=2);p.add_argument("--build-only",action="store_true");a=p.parse_args()
    c=config();cpu=arch();system={"Windows":"windows","Linux":"linux","Darwin":"macos"}[platform.system()]
    build=ROOT/"build"/f"ci-{system}-{cpu}";sdk=Path(a.qt_root).resolve();stage=ROOT/"build"/f"stage-{system}-{cpu}";out=ROOT/"dist/release";out.mkdir(parents=True,exist_ok=True)
    args=["cmake","-S",ROOT,"-B",build,"-G","Ninja","-DCMAKE_BUILD_TYPE=Release","-DCMAKE_TRY_COMPILE_CONFIGURATION=Release",f"-DCMAKE_PREFIX_PATH={sdk}","-DBUILD_TESTING=ON",f'-D{c["environment"]}_BUNDLE_RUNTIME=ON']
    if system=="linux":args+=["-DCMAKE_CXX_COMPILER=g++-12"]
    if system=="macos":args+=[f'-DCMAKE_OSX_DEPLOYMENT_TARGET={c["macosMinimum"]}']
    if c["app"]=="8bitPad":
        for n,d in [("ads","ads"),("SingleApplication","singleapplication"),("editorconfig_core_qt","editorconfig-core-qt")]:args+=[f"-DCPM_{n}_SOURCE={ROOT/'dependency-sources'/d}"]
        args+=["-DFETCHCONTENT_FULLY_DISCONNECTED=ON"]
    else:
        helper=ROOT/"build"/f"puttygen-{system}-{cpu}"/("puttygen.exe" if system=="windows" else "puttygen")
        if not helper.exists():run([sys.executable,ROOT/"scripts/ci/build-puttygen.py","--output",helper])
        run([sys.executable,ROOT/"scripts/ci/build-puttygen.py","--verify",helper]);args+=[f"-DBIT8SHELL_PUTTYGEN_EXECUTABLE={helper}"]
    run(args);run(["cmake","--build",build,"--parallel",a.jobs])
    if system=="windows" and c["app"]=="8bitShell":openssh_windows(build,cpu,build_layout=True)
    env=os.environ.copy();env.update(QT_QPA_PLATFORM="offscreen",QT_QUICK_BACKEND="software")
    env["PATH"]=str(sdk/"bin")+os.pathsep+env["PATH"]
    env["QT_PLUGIN_PATH"]=str(sdk/"plugins")
    env["QML_IMPORT_PATH"]=str(sdk/"qml")
    env["QML2_IMPORT_PATH"]=str(sdk/"qml")
    testtemp=build/"test-temp";testtemp.mkdir(exist_ok=True)
    if system=="windows":
        env["TEMP"]=str(testtemp);env["TMP"]=str(testtemp)
    else:
        # QLocalServer file sockets live in the temp directory. sun_path is only
        # 108 bytes, and the workspace path is longer than that.
        # PrivateFile walks every parent with O_NOFOLLOW. On macOS /tmp is a
        # symlink to /private/tmp, so use the real path or private saves fail.
        short=Path("/tmp").resolve()/"b8-ci";short.mkdir(mode=0o700,exist_ok=True)
        env["TEMP"]=str(short);env["TMP"]=str(short);env["TMPDIR"]=str(short)
    if system=="windows":env["QT_QPA_FONTDIR"]=str(Path(os.environ["WINDIR"])/"Fonts")
    elif system=="linux":env["QT_QPA_FONTDIR"]="/usr/share/fonts/truetype/dejavu"
    run(["ctest","--test-dir",build,"--output-on-failure"],env=env)
    if a.build_only:return
    if stage.exists():raise ValueError("Use a fresh stage directory; previous packages are preserved")
    run(["cmake","--install",build,"--prefix",stage])
    if system=="linux":
        if c["app"]=="8bitShell":
            ssh=stage/"bin/openssh";ssh.mkdir(parents=True)
            for n in ("ssh","sftp","ssh-keygen","scp"):shutil.copy2(Path("/usr/bin")/n,ssh/n)
        libdir=stage/"lib"
        if libdir.is_dir():
            for archive in libdir.glob("*.a"):archive.unlink()
        platforms=stage/"plugins/platforms";platforms.mkdir(parents=True,exist_ok=True)
        offscreen=sdk/"plugins/platforms"/"libqoffscreen.so"
        if offscreen.is_file():shutil.copy2(offscreen,platforms/offscreen.name)
        bundle_linux(stage);launcher(stage)
    elif system=="windows":
        crt_windows(stage,cpu)
        if c["app"]=="8bitShell":openssh_windows(stage,cpu)
        (stage/"bin/qt.conf").write_text("[Paths]\nPrefix=..\nPlugins=plugins\nQmlImports=qml\n")
        (stage/"Start-Portable.cmd").write_text(f'@echo off\nset "{c["environment"]}_PROFILE=%~dp0profile"\nstart "" "%~dp0bin\\{c["app"]}.exe" %*\n')
        (stage/"Start.cmd").write_text(f'@echo off\nstart "" "%~dp0bin\\{c["app"]}.exe" %*\n')
    else:
        app=stage/(c["app"]+".app")
        shutil.copytree(stage/"share",app/"Contents/Resources/distribution-notices",dirs_exist_ok=True)
        shutil.copytree(sdk/"share/8bit-qt-sources",app/"Contents/Resources/distribution-notices/qt",dirs_exist_ok=True)
        offscreen=app/"Contents/PlugIns/platforms/libqoffscreen.dylib"
        offscreen.parent.mkdir(parents=True,exist_ok=True)
        shutil.copy2(sdk/"plugins/platforms/libqoffscreen.dylib",offscreen)
        deploy=[sdk/"bin/macdeployqt",app,f"-executable={offscreen}"]
        if c["app"]=="8bitShell":deploy+=[f"-executable={app/'Contents/MacOS/8bitShell-askpass'}"]
        run(deploy)
        sign=os.environ.get("MACOS_SIGN_IDENTITY","-")
        run(["codesign","--force","--deep","--options","runtime","--timestamp","--entitlements",ROOT/"packaging/macos-entitlements.plist","--sign",sign,app] if sign!="-" else ["codesign","--force","--deep","--sign","-",app])
        run(["codesign","--verify","--deep","--strict",app])
        (stage/"Start-Portable.command").write_text(f'#!/bin/sh\nset -eu\nbase=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd -P)\nexport {c["environment"]}_PROFILE="$base/profile"\nexec "$base/{c["app"]}.app/Contents/MacOS/{c["app"]}" "$@"\n')
        (stage/"Start-Portable.command").chmod(0o755)
    if system=="linux":
        from runtime_sources import collect
        collect(stage,out,cpu)
    if system!="macos":shutil.copytree(sdk/"share/8bit-qt-sources",stage/"share/qt-licenses",dirs_exist_ok=True)
    payload=stage/(c["app"]+".app")/"Contents/MacOS"/c["app"] if system=="macos" else stage/"bin"/(c["app"]+(".exe" if system=="windows" else ""))
    verify_binary(payload,system,cpu)
    smoke(stage,system)
    archive_tree(stage,out,system,cpu)
    if system=="linux":linux_packages(stage,out,cpu)
    elif system=="windows":run(["powershell.exe","-NoProfile","-ExecutionPolicy","Bypass","-File",ROOT/"scripts/ci/package-windows.ps1","-Stage",stage,"-OutputDirectory",out,"-Architecture",cpu])
    else:
        base=f'{c["app"]}-{version()}-macos-{cpu}';dmgroot=ROOT/"build"/f"dmg-{cpu}";dmgroot.mkdir()
        run(["ditto",stage/(c["app"]+".app"),dmgroot/(c["app"]+".app")])
        (dmgroot/"Applications").symlink_to("/Applications")
        run(["hdiutil","create","-volname",c["app"],"-srcfolder",dmgroot,"-ov","-format","UDZO",out/(base+".dmg")])
        if os.environ.get("MACOS_NOTARY_APPLE_ID"):
            run(["xcrun","notarytool","submit",out/(base+".dmg"),"--apple-id",os.environ["MACOS_NOTARY_APPLE_ID"],"--team-id",os.environ["MACOS_NOTARY_TEAM_ID"],"--password",os.environ["MACOS_NOTARY_PASSWORD"],"--wait"],stdout=subprocess.DEVNULL)
            run(["xcrun","stapler","staple",out/(base+".dmg")])
    if system=="linux" and cpu=="x64":
        run(["git","archive","--format=tar.gz",f'--prefix={c["app"]}-{version()}-source/',"HEAD","-o",out/f'{c["app"]}-{version()}-source.tar.gz'],cwd=ROOT)
        with tarfile.open(out/f'{c["app"]}-{version()}-qt-{c["qtVersion"]}-source.tar.gz',"w:gz") as t:
            for item in sorted((ROOT/"tooling/qt-sources").glob("*")):t.add(item,arcname=item.name)
    if system=="linux":
        source_entries=[]
        from distribution import digest
        for item in sorted(out.glob(f'{c["app"]}-{version()}-*source.tar.gz')):
            source_entries.append({"name":item.name,"sha256":digest(item),"size":item.stat().st_size,"os":"source","arch":"all","version":version(),"glibc":None,"experimental":False})
        (out/f"manifest-source-linux-{cpu}.json").write_text(json.dumps(source_entries,indent=2)+"\n")
    stamp_assets(out,system,cpu)
    print(f'{c["app"]} {version()} {system}/{cpu}: built, tested, packaged')
if __name__=="__main__":
    try:main()
    except (subprocess.CalledProcessError,subprocess.TimeoutExpired):raise
    except Exception as exc:
        _hosted_error(f"{type(exc).__name__}: {exc}");raise
