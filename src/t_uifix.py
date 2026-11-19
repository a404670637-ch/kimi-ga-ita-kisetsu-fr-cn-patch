# -*- coding: utf-8 -*-
"""UI 文本内存补丁实验工具（需在标题画面/对应界面停留时运行）。

原理：与选项扫描相同 —— 引擎的自绘 UI 文字在内存里是 SJIS 明文，就地改写成中文（不可编码的
汉字走隧道码，隧道码在 UI 绘制路径上同样会被 cnpatch 的字形钩子翻译成宋体字形）。
用法：
  python t_uifix.py <pid> list            # 只列出可命中的串
  python t_uifix.py <pid> patch           # 扫描并改写，记录到 uifix_record.json
  python t_uifix.py <pid> restore         # 按记录还原
"""
import ctypes, ctypes.wintypes as wt, sys, json, os
sys.stdout.reconfigure(encoding='utf-8', errors='replace')
BASE = os.path.dirname(os.path.abspath(__file__))
UNI = r"G:\age\Kimi ga Ita Kisetsu Complete\cnpatch\cnpatch_uni.bin"

DICT = {
    '最初から': '开始游戏',
    '続きから': '继续游戏',
    'プレミア': '特典',
    'コンフィグ': '设置',
    '終了': '退出',
    'セーブ': '保存',
    'ロード': '读取',
    'バックログ': '回想',
    'テキスト自動送り': '自动播放',
    '既読テキストスキップ': '跳过已读',
    'タイトルにもどる': '返回标题',
    'セーブデータ選択': '选择存档',
    'セーブ中': '保存中',
}

def load_uni():
    d = open(UNI, 'rb').read()
    n = int.from_bytes(d[4:8], 'little')
    code2uni = {}
    uni2code = {}
    for i in range(n):
        c = int.from_bytes(d[8+i*6:10+i*6], 'little')
        u = int.from_bytes(d[10+i*6:14+i*6], 'little')
        code2uni[c] = u
        uni2code[u] = c
    return code2uni, uni2code

def encode_cn(s, uni2code):
    out = bytearray()
    for ch in s:
        try:
            b = ch.encode('cp932')
            if len(b) == 1 or (len(b) == 2):
                out += b
                continue
        except Exception:
            pass
        c = uni2code.get(ord(ch))
        if c is None:
            raise ValueError('no tunnel code for %r' % ch)
        out += bytes([c >> 8, c & 0xFF])
    return bytes(out)

def regions(k, h):
    class MBI(ctypes.Structure):
        _fields_=[('BaseAddress',ctypes.c_void_p),('AllocationBase',ctypes.c_void_p),('AllocationProtect',wt.DWORD),
                  ('PartitionId',wt.WORD),('_p',wt.WORD),('RegionSize',ctypes.c_size_t),('State',wt.DWORD),
                  ('Protect',wt.DWORD),('Type',wt.DWORD),('_p2',wt.DWORD)]
    addr=0; MEM_COMMIT=0x1000
    while addr < 0x7FFF0000:
        m=MBI()
        if not k.VirtualQueryEx(h, ctypes.c_void_p(addr), ctypes.byref(m), ctypes.sizeof(m)): break
        base=ctypes.cast(m.BaseAddress, ctypes.c_void_p).value or 0
        size=int(m.RegionSize); prot=int(m.Protect)
        if m.State==MEM_COMMIT and 0<size<64*1024*1024 and not (prot&0x101) and not (prot&0x10):
            buf=ctypes.create_string_buffer(size); got=ctypes.c_size_t()
            if k.ReadProcessMemory(h, ctypes.c_void_p(base), buf, ctypes.c_size_t(size), ctypes.byref(got)):
                yield base, bytes(buf[:got.value])
        addr = base+size if size else addr+0x1000

def main():
    pid=int(sys.argv[1]); mode=sys.argv[2]
    k=ctypes.windll.kernel32; h=k.OpenProcess(0x1F0FFF, False, pid)
    code2uni, uni2code = load_uni()
    plan=[]
    for jp, cn in DICT.items():
        try:
            jb=jp.encode('cp932')
        except Exception:
            print("skip (jp not cp932)", jp); continue
        try:
            cb=encode_cn(cn, uni2code)
        except Exception as e:
            print("skip (cn encode fail)", jp, cn, e); continue
        if len(cb) > len(jb):
            print("skip (too long) %s -> %s  %d > %d bytes" % (jp, cn, len(cb), len(jb))); continue
        plan.append((jp, jb, cb))
    if mode=='list':
        for jp, jb, cb in plan:
            hits=[]
            for base, d in regions(k, h):
                i=d.find(jb)
                while i>=0:
                    hits.append(base+i); i=d.find(jb,i+1)
            print("%-12s -> %-6s  jp=%dB cn=%dB  hits=%d %s" % (jp, DICT[jp], len(jb), len(cb), len(hits), [hex(x) for x in hits[:4]]))
        return
    if mode=='patch':
        rec=[]
        for jp, jb, cb in plan:
            n=0
            for base, d in regions(k, h):
                i=d.find(jb)
                while i>=0:
                    a=base+i
                    old=ctypes.create_string_buffer(len(jb))
                    k.ReadProcessMemory(h, ctypes.c_void_p(a), old, len(jb), None)
                    if bytes(old[:len(jb)])==jb:
                        CT=0x40; o=wt.DWORD()
                        k.VirtualProtectEx(h, ctypes.c_void_p(a), len(jb), CT, ctypes.byref(o))
                        pad=cb + b'\x00'*(len(jb)-len(cb)) if False else cb
                        ok=k.WriteProcessMemory(h, ctypes.c_void_p(a), pad, len(cb), None)
                        k.VirtualProtectEx(h, ctypes.c_void_p(a), len(jb), o.value, ctypes.byref(o))
                        if ok: n+=1; rec.append({'addr':hex(a),'old':jb.hex(),'len':len(jb)})
                    i=d.find(jb,i+1)
            print("patched %-12s x%d" % (jp, n))
        json.dump(rec, open(os.path.join(BASE,'uifix_record.json'),'w'))
        print("record saved:", len(rec))
        return
    if mode=='restore':
        rec=json.load(open(os.path.join(BASE,'uifix_record.json')))
        n=0
        for r in rec:
            a=int(r['addr'],16); b=bytes.fromhex(r['old'])
            o=wt.DWORD()
            k.VirtualProtectEx(h, ctypes.c_void_p(a), len(b), 0x40, ctypes.byref(o))
            if k.WriteProcessMemory(h, ctypes.c_void_p(a), b, len(b), None): n+=1
            k.VirtualProtectEx(h, ctypes.c_void_p(a), len(b), o.value, ctypes.byref(o))
        print("restored", n)
        return
main()
