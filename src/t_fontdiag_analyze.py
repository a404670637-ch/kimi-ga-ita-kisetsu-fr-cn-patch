# -*- coding: utf-8 -*-
"""分析 fontlog.txt：找出指定假名/汉字序列的调用方返回地址分布。"""
import sys, os, collections
sys.stdout.reconfigure(encoding='utf-8', errors='replace')
LOG=r"G:\age\Kimi ga Ita Kisetsu Complete\cnpatch\fontlog.txt"
if not os.path.exists(LOG):
    print("fontlog.txt 不存在：游戏未以 fontdiag=1 启动？"); sys.exit(1)
seqs={
 'まりか(82DC)': 0x82DC,
 'えい(82A6)': 0x82A6,
}
rows=[]
with open(LOG,'rb') as f:
    for line in f:
        parts=line.split()
        if len(parts)>=2:
            try:
                code=int(parts[0],16); ra=parts[1].decode('ascii','ignore')
            except Exception:
                continue
            rows.append((code,ra))
print("fontlog lines:", len(rows))
cnt=collections.Counter(c for c,_ in rows)
for name,code in seqs.items():
    print("%s appears %d times" % (name, cnt.get(code,0)))
# 调用方分布（全部）
ra_cnt=collections.Counter(ra for c,ra in rows)
print("distinct callers:", len(ra_cnt))
for ra,n in ra_cnt.most_common(12):
    print("  ", ra, n)
# 找出假名附近的行，打印上下文
for i,(c,ra) in enumerate(rows):
    if c==0x82DC:  # ま
        ctx=[hex(x) for x,_ in rows[max(0,i-3):i+5]]
        print("まりか context at", i, ctx, "caller", ra)
        break
