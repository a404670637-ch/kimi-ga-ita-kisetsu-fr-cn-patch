# 多次读档闪退（Vm60+0x300AE）根因与修复记录

版本：v1.24（2026-11-23）
相关代码：`src/cnpatch.c` 的 `capture_text()`；产物 `cnpatch/cnpatch.dll` e09a31b6…0c43cb01

## 现象

读取已经走过的剧情位置，反复读档数次后游戏直接闪退（无提示）。
Windows 事件日志：`rugp.exe` 0xC0000005，故障模块 `Vm60.dll` 偏移 `0x300AE`。

## 第一现场（外挂调试器，WOW64）

```
11-23 21:38 tid=14704 eip=100300AE esi=0x1347C6F4 edx=0x81036381 ebx(key)=0x1794 edi=0x43D
esi 前后 1KB 落盘后可见：0x1347C6F4 落在节点文本的日文残尾
「…にやよいさんのことなんか………。」上（SJIS 0x81 0x63 = …）。
节点实际步进 0x48（0x14 + align4(50)），遍历计算步进 0x40（0x14 + align4(41)）。
更早的三份 dump（11-21 / 11-22×2）的 [esi] 同样是日文残尾：
0x4881CC82 =「の？」、0x00017681 =「」+0x01、0x00000176 = 长度值。
```

## 反汇编（Vm60 内存转储，base 0x10000000）

```
1003009c  mov  ecx,[esi+8]      ; 节点 key（20-bit）
100300a6  cmp  ecx,ebx          ; 命中返回
100300aa  mov  edx,[esi]        ; vtable
100300ae  call *[edx]           ; 取类描述符（返回 0x100B9B00）
100300b6  call *0x24(eax)       ; vtable[9] = 动态大小
100300b9  mov  ecx,[ebp+4]      ; 类描述符 +4 = 固定头大小 0x14
100300bc  add  ecx,eax
100300be  add  esi,ecx          ; 步进
```

大小函数 `Vm60+0x79340`：

```
mov edx,ecx
push edi
or ecx,-1
xor eax,eax
lea edi,[edx+0x14]      ; 文本指针
repnz scasb             ; strlen(text)，扫到 0x00
...
lea eax,[eax+ecx*4]     ; 4*ceil(len/4)
```

结论：**遍历步长 = 0x14 + align4(strlen(当前文本))**。

## 根因

补丁在 `capture_text` 里原地缩短文本（中文通常比日文短），而节点的实际占位是按日文长度分配的。
遍历时按缩短后的中文长度计算，每个节点少跳 (日文长−中文长) 字节；读档重建剧情缓存时误差累积，
`esi` 最终漂移进字符串数据，把日文残尾当作 vtable 解引用 → 崩溃。
这也解释了「为什么要读几次档才崩」：单次漂移很小，累积到跨出节点区才触发。

## 修复

`capture_text()` 替换时保持原字符串字节长度不变：

```c
body = vlen; if (val[vlen-1]==0x01) body--;      /* 中文正文（不含结束符） */
memcpy(s, val, body);
for (i = body; i + 1 < ol; i++) s[i] = 0x01;      /* 0x01 填充到原长度 */
s[ol-1] = 0x01; s[ol] = 0;                        /* 原末字节与 NUL 保持原位 */
```

- `strlen(s)` 与原句完全相同 → 步长一致 → 不再漂移。
- 渲染在第一个 `0x01` 处停止，填充不可见（实机确认行尾无异常）。
- 存档备注的 storefix 匹配条件放宽为 `len == 中文长度 || len == 原日文长度`。

## 验证

- 实机（2026-11-23 21:47）：同一读档场景反复读档，不再闪退；正文/回味显示正常。
- 外挂调试器：崩溃点不再出现。
- 数据未变：`cnpatch_txt.bin`（44,138 条）、`cnpatch_uni.bin`（3,083 隧道字）。

## 诊断手段（保留）

`config.ini` 的 `diag=1`（默认 0）会在进程内注册 VEH，把第一次 AV 的寄存器、
`esi` 前后内存 hexdump、最近 storefix 写入与 SetText 缓冲记录到 `cnpatch/avlog.txt`。
注意：若 VEH 内部再次崩溃会得到 0 字节 dump，本版已加范围裁剪与重入锁。
