# -*- coding: utf-8 -*-
"""按最新 savebak7_* 的 MANIFEST 还原存档目录，并逐文件校验。"""
import os, shutil, hashlib, glob, sys
sys.stdout.reconfigure(encoding='utf-8', errors='replace')
DST=r"C:\Users\40467\Documents\age\君がいた季節FR\Vmreg"
BASE=r"C:\Users\40467\Documents\deepseek-harness\default-workspace\work\p5"
snaps=sorted(glob.glob(os.path.join(BASE,"savebak7_*","Vmreg")))
if not snaps:
    print("没有 savebak7_* 快照"); sys.exit(1)
src=snaps[-1]
man=os.path.join(os.path.dirname(src),"MANIFEST.sha256")
def sha(p):
    h=hashlib.sha256()
    with open(p,'rb') as f:
        for b in iter(lambda: f.read(1<<20), b''): h.update(b)
    return h.hexdigest()
want={}
for line in open(man,encoding='utf-8'):
    line=line.strip()
    if not line: continue
    h,fn=line.split("  ",1); want[fn]=h
ok=bad=0
for fn,h in want.items():
    s=os.path.join(src,fn); d=os.path.join(DST,fn)
    shutil.copy2(s,d)
    if sha(d)==h: ok+=1
    else: bad+=1; print("HASH MISMATCH", fn)
print("restored", ok, "ok,", bad, "bad, from", src)
