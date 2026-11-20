#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""dump_bilingual.py — 把 cnpatch_txt.bin / cnpatch_choice.bin 导出成「日文原文 <TAB> 中文译文」。

不需要游戏本体，只要有本仓库 cnpatch/ 目录下这两个文件即可。

用法：
    python dump_bilingual.py                        # 打印到标准输出
    python dump_bilingual.py -o bilingual.tsv       # 写入文件
    python dump_bilingual.py --format csv -o a.csv  # CSV（自动加引号）
    python dump_bilingual.py --choice -o choice.tsv # 只导出路线选项表

文件格式（小端）：
    CNTX 表：'CNTX' + u32 记录数 + u32 偏移表[记录数] + 这里开始的数据块
             每条记录 = u16 keylen + key 字节 + u16 vallen + val 字节
    CNUN 表：'CNUN' + u32 条目数 + 条目[条目数]，每条 = u16 隧道码 + u32 Unicode

编码说明：
    正文用 Shift-JIS（cp932）存放，其中「隧道码」是补丁自造的伪 SJIS 双字节码，
    需要用 CNUN 表映射回真正的 Unicode 汉字；本脚本会自动完成这一步。
    控制字符：0x01 = 字符串结束（已去掉），0x03 = 引擎的停顿分隔符（这里输出为空格）。
"""
import argparse
import csv
import io
import struct
import sys


def load_uni(path):
    """读 CNUN：隧道码 -> Unicode。"""
    data = open(path, 'rb').read()
    if data[:4] != b'CNUN':
        raise SystemExit('%s: 不是 CNUN 表' % path)
    (count,) = struct.unpack_from('<I', data, 4)
    table = {}
    for i in range(count):
        code, uni = struct.unpack_from('<HI', data, 8 + i * 6)
        table[code] = uni
    return table


def decode(blob, uni):
    """把 SJIS 字节解成文本：双字节码先查隧道表，查不到再用 cp932。"""
    out = []
    i = 0
    n = len(blob)
    while i < n:
        b = blob[i]
        if b < 0x80:
            out.append(chr(b))
            i += 1
            continue
        if i + 1 >= n:
            out.append('?')
            break
        code = (b << 8) | blob[i + 1]
        i += 2
        if code in uni:
            out.append(chr(uni[code]))
            continue
        try:
            out.append(bytes((b, blob[i - 1])).decode('cp932'))
        except UnicodeDecodeError:
            out.append('?')
    text = ''.join(out).replace('\x01', '').replace('\x03', ' ')
    # 原句可能跨行：统一转成字面量 \n，保证一条记录占一行
    text = text.replace('\r\n', '\n').replace('\r', '\n').replace('\n', '\\n')
    return text.strip()


def iter_pairs(txt_path, uni):
    """迭代 CNTX 表里的 (日文, 中文)。"""
    data = open(txt_path, 'rb').read()
    if data[:4] != b'CNTX':
        raise SystemExit('%s: 不是 CNTX 表' % txt_path)
    (count,) = struct.unpack_from('<I', data, 4)
    offsets = struct.unpack_from('<%dI' % count, data, 8)
    base = 8 + 4 * count
    for off in offsets:
        p = base + off
        (klen,) = struct.unpack_from('<H', data, p)
        key = data[p + 2:p + 2 + klen]
        q = p + 2 + klen
        (vlen,) = struct.unpack_from('<H', data, q)
        val = data[q + 2:q + 2 + vlen]
        yield decode(key, uni), decode(val, uni)


def iter_choice(choice_path, uni):
    """Choice table uses the CNCH layout: 'CNCH' + u32 count + records.
    Each record is u16 keylen + key + u16 vallen + val (same as a body record,
    but there is no offset table)."""
    data = open(choice_path, 'rb').read()
    if data[:4] != b'CNCH':
        raise SystemExit('%s: not a CNCH table' % choice_path)
    (count,) = struct.unpack_from('<I', data, 4)
    p = 8
    for _ in range(count):
        (klen,) = struct.unpack_from('<H', data, p)
        key = data[p + 2:p + 2 + klen]
        q = p + 2 + klen
        (vlen,) = struct.unpack_from('<H', data, q)
        val = data[q + 2:q + 2 + vlen]
        yield decode(key, uni), decode(val, uni)
        p = q + 2 + vlen


def main():
    ap = argparse.ArgumentParser(description='导出 cnpatch 的中日对照文本')
    ap.add_argument('-o', '--out', help='输出文件（默认打印到标准输出）')
    ap.add_argument('--dir', default='.', help='cnpatch 目录，默认当前目录')
    ap.add_argument('--txt', default='cnpatch_txt.bin', help='正文表文件名')
    ap.add_argument('--uni', default='cnpatch_uni.bin', help='隧道码表文件名')
    ap.add_argument('--choice', default='cnpatch_choice.bin', help='选项表文件名')
    ap.add_argument('--choice-only', action='store_true', help='只导出选项表')
    ap.add_argument('--format', choices=('tsv', 'csv'), default='tsv')
    ap.add_argument('--skip-empty', action='store_true', help='跳过译文为空的记录')
    args = ap.parse_args()

    import os
    uni = load_uni(os.path.join(args.dir, args.uni))
    if args.choice_only:
        pairs = iter_choice(os.path.join(args.dir, args.choice), uni)
    else:
        pairs = iter_pairs(os.path.join(args.dir, args.txt), uni)

    buf = io.StringIO()
    if args.format == 'csv':
        w = csv.writer(buf, lineterminator='\n')
        buf.write('jp,zh\n')
        for jp, zh in pairs:
            if args.skip_empty and not zh:
                continue
            w.writerow([jp, zh])
    else:
        buf.write('jp\tzh\n')
        for jp, zh in pairs:
            if args.skip_empty and not zh:
                continue
            buf.write('%s\t%s\n' % (jp.replace('\t', ' '), zh.replace('\t', ' ')))

    text = buf.getvalue()
    if args.out:
        open(args.out, 'w', encoding='utf-8', newline='').write(text)
        sys.stderr.write('已写出 %s（%d 行）\n' % (args.out, text.count('\n') - 1))
    else:
        sys.stdout.write(text)


if __name__ == '__main__':
    main()
