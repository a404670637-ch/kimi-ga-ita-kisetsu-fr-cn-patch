
import struct,os,json
BASE=r'C:\Users\40467\Documents\deepseek-harness\default-workspace\work\p5'
UNI=os.path.join(BASE,'dist','cnpatch','cnpatch_uni.bin')
OUTDIR=os.path.join(BASE,'dist','cnpatch')
choice_file=os.path.join(BASE,'choice','choice.jsonl')
data=open(UNI,'rb').read()
assert data[:4]==b'CNUN', data[:4]
n=struct.unpack_from('<I',data,4)[0]
entries=[]; off=8
for i in range(n):
    code,uni=struct.unpack_from('<HI',data,off); off+=6
    entries.append((code,uni))
used={c for c,_ in entries}
u2c={u:c for c,u in entries}
def valid_sjis(code):
    lead=code>>8; trail=code&0xFF
    if not (0x81<=lead<=0x9F or 0xE0<=lead<=0xFC): return False
    return 0x40<=trail<=0x7E or 0x80<=trail<=0xFC
free=[]
for lo,hi in ((0x989F,0x9FFD),(0xE040,0xEAA5)):
    for c in range(lo,hi):
        if valid_sjis(c) and c not in used: free.append(c)
print('base uni entries',n,'free codes',len(free))
nxt=[0]
def code_for(ch):
    if ch in u2c: return u2c[ch]
    c=free[nxt[0]]; nxt[0]+=1
    u2c[ch]=c; entries.append((c,ch)); used.add(c)
    return c
def enc(s):
    out=bytearray()
    for ch in s:
        o=ord(ch)
        if o<0x80:
            out.append(o)
        else:
            out+=struct.pack('>H',code_for(o))
    return bytes(out)
pairs=[]
for line in open(choice_file,encoding='utf-8'):
    line=line.strip()
    if not line: continue
    r=json.loads(line)
    pairs.append((r['jp'],r['zh']))
recs=[]
for jp,zh in pairs:
    key=jp.encode('cp932')
    val=enc(zh)
    if len(val)>len(key):
        print('SKIP too long',jp,zh,len(val),'>',len(key)); continue
    recs.append((key,val))
print('choices',len(recs))
blob=bytearray(b'CNCH'); blob+=struct.pack('<I',len(recs))
for k,v in recs:
    blob+=struct.pack('<H',len(k))+k+struct.pack('<H',len(v))+v
open(os.path.join(OUTDIR,'cnpatch_choice.bin'),'wb').write(blob)
print('choice bin bytes',len(blob))
out=bytearray(b'CNUN'); out+=struct.pack('<I',len(entries))
for c,u in sorted(entries,key=lambda x:x[0]):
    out+=struct.pack('<HI',c,u)
open(os.path.join(OUTDIR,'cnpatch_uni.bin'),'wb').write(out)
print('uni bin bytes',len(out),'entries',len(entries))