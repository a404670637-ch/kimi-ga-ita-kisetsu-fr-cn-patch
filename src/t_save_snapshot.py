# -*- coding: utf-8 -*-
"""快照存档目录到 work/p5/savebak7_<时间>，并生成 MANIFEST.sha256。"""
import os, shutil, hashlib, time, sys
sys.stdout.reconfigure(encoding='utf-8', errors='replace')
SRC=r"C:\Users\40467\Documents\age\君がいた季節FR\Vmreg"
BASE=r"C:\Users\40467\Documents\deepseek-harness\default-workspace\work\p5"
dst=os.path.join(BASE, "savebak7_"+time.strftime("%Y%m%d_%H%M%S"), "Vmreg")
os.makedirs(dst, exist_ok=True)
def sha(p):
    h=hashlib.sha256()
    with open(p,'rb') as f:
        for b in iter(lambda: f.read(1<<20), b''): h.update(b)
    return h.hexdigest()
lines=[]
for fn in sorted(os.listdir(SRC)):
    s=os.path.join(SRC,fn); d=os.path.join(dst,fn)
    if os.path.isfile(s):
        shutil.copy2(s,d)
        lines.append("%s  %s" % (sha(d), fn))
man=os.path.join(os.path.dirname(dst),"MANIFEST.sha256")
open(man,'w',encoding='utf-8').write("\n".join(lines)+"\n")
print("snapshot ->", dst)
print("files", len(lines), "manifest", man)
