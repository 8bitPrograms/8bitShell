#!/usr/bin/env python3
"""Build an architecture-native, cached Qt SDK from versioned official sources."""
import argparse,json,os,platform,shutil
from pathlib import Path
from distribution import ROOT,config,download,extract,run,arch,digest
def build(prefix,jobs):
    c=config();v=c["qtVersion"];prefix=Path(prefix).resolve()
    modules=["qtbase","qtshadertools","qtsvg","qt5compat","qtlanguageserver","qtdeclarative","qttools"]
    if platform.system()=="Linux":modules+=["qtwayland"]
    marker=prefix/".8bit-sdk.json"
    desired={"version":v,"arch":arch(),"system":platform.system(),"modules":modules,"revision":2}
    if marker.exists() and json.loads(marker.read_text())==desired:return prefix
    if prefix.exists() and any(prefix.iterdir()):raise ValueError("Incomplete or mismatched SDK: use a fresh cache key/directory")
    prefix.mkdir(parents=True,exist_ok=True);cache=ROOT/"tooling/qt-sources";work=ROOT/"build/qt-source"
    for module in modules:
        name=f"{module}-everywhere-src-{v}.tar.xz";url=f"https://download.qt.io/archive/qt/{v.rsplit('.',1)[0]}/{v}/submodules/{name}"
        checksum=download(url+".sha256",cache/(name+".sha256")).read_text().split()[0]
        if len(checksum)!=64:raise ValueError("Invalid official Qt checksum")
        source_archive=download(url,cache/name,checksum);src=work/(name.removesuffix(".tar.xz"))
        if not src.exists():extract(source_archive,work)
        builddir=work/(module+"-build")
        args=["cmake","-S",src,"-B",builddir,"-G","Ninja","-DCMAKE_BUILD_TYPE=Release","-DCMAKE_TRY_COMPILE_CONFIGURATION=Release",f"-DCMAKE_INSTALL_PREFIX={prefix}",f"-DCMAKE_PREFIX_PATH={prefix}","-DCMAKE_INSTALL_LIBDIR=lib","-DQT_BUILD_TESTS=OFF","-DQT_BUILD_EXAMPLES=OFF","-DQT_BUILD_DOCS=OFF"]
        if module=="qtbase":
            args+=["-DFEATURE_testlib=ON","-DFEATURE_widgets=ON","-DFEATURE_printsupport=ON"]
            if platform.system()=="Linux":args+=["-DINPUT_openssl=linked","-DFEATURE_xcb=ON","-DFEATURE_fontconfig=ON"]
        if module=="qttools":args+=["-DFEATURE_clang=OFF","-DFEATURE_assistant=OFF","-DFEATURE_designer=OFF","-DFEATURE_distancefieldgenerator=OFF"]
        if platform.system()=="Darwin":args+=[f'-DCMAKE_OSX_DEPLOYMENT_TARGET={c["macosMinimum"]}']
        run(args);run(["cmake","--build",builddir,"--parallel",str(jobs)]);run(["cmake","--install",builddir])
        notices=prefix/"share/8bit-qt-sources"/module;notices.mkdir(parents=True,exist_ok=True)
        for p in src.glob("LICENSE*"):
            if p.is_dir():shutil.copytree(p,notices/p.name,dirs_exist_ok=True)
            else:shutil.copy2(p,notices/p.name)
    marker.write_text(json.dumps(desired,indent=2)+"\n")
    return prefix
if __name__=="__main__":
    p=argparse.ArgumentParser();p.add_argument("--prefix",required=True);p.add_argument("--jobs",type=int,default=2);a=p.parse_args();print(build(a.prefix,a.jobs))
