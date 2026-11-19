# 回味（BACK LOG）显示日文与名字乱码：2026-11-20 修复记录

## 一、已修复：名牌字被画成别的汉字（【６りか】、「喘」等）

### 症状
- 回味里 `【まりか】` 显示成 `【６りか】`；
- 正文里 `ま` 显示成 `６`、`き`→`喘`、`睦`→`朦`、`月`→`灾`、`優`→`一` 等。

### 根因（已确证）
`work/p5/cnpatch.c` 的 `build_rmap()` 用
`MultiByteToWideChar(932, tunnel_code)` 得到的 Unicode 作**反向映射键**，
而 `my_GetGlyphOutlineA` 收到的是**原始两字节 SJIS 码**。
例：隧道码 0xE49C 映射到目标字「６」(U+FF16)，但 cp932(0xE49C)=「苜」=U+82DC，
恰好等于「ま」的 SJIS 码 0x82DC → `find_rev(0x82DC)` 命中 → 所有原生「ま」被画成「６」。

影响面（旧 cnpatch_uni.bin）：868 个原始 SJIS 码与 rmap 键冲突，其中 370 个真出现在语料；
名字里命中 ま→６、き→喘、睦→朦、月→灾、優→一；另有 `：％９ＣＥＫＬＲＸＹ ぁきぎそぴほまむめゃゅ`
以及 174 个隧道码与原日文文本使用的码重叠（如 98A5=丼→那）。

### 修复（构建期，work/p5/build_patch.py）
新增 `_safe_code()`：隧道码必须同时满足
1. 不在原日文语料的 used 集合；
2. cp932(码) 的字符不在语料/名牌字符集；
3. 若该 Unicode 值本身是合法 SJIS 双字节码，则它编码出的字符也不能被绘制。

池序：先 0x989F–0x9FFD、0xE040–0xEAA5，不足补 0x8200–0x9800、0xEAA5–0xEFFD。

重建结果：records 44138 / overflow 0 / missing 1 / np_dropped 0 / tunnel 3091。
- cnpatch_txt.bin sha256 `b10e6f47d2fe4a0121dd01236fd4cd71099bf0efea0c3f5c38fefc95a359957b`
- cnpatch_uni.bin sha256 `af7eb7e36351f7ff64d5d39846a850096238579db81f5cb97709943a820e85d9`

### 连带修复：逐人物字色
`work/p5/namemap.h` 里硬编码的中文名字节是旧隧道码，重编号后 `colour_compare` 匹配不上 → 颜色回落。
已用 `work/p5/t_gen_namemap.py` 从新表提取 `【name】` 对重新生成（175 组），旧文件备份 `namemap.h.bak_old`。
用户实测确认字色已正确。

## 二、已修复：回味整体显示日文

v1.18 的 storefix 会把引擎侧所有副本还原成日文，包括**当前句的显示缓冲**；回味打开时读的就是这份缓冲，
所以回味整页都是日文。新增 `keepzh=1`：
- `restore_text2()` 不再把显示缓冲写回日文（`!g_keepzh` 条件）；
- `patch_copy_hit()` 跳过显示缓冲本身（`p == g_origptr`）与引擎的每行显示节点
  （节点头 `[p-0x10] == p+0x18` 的内联缓冲节点）。

存档备注走的是另一种结构（`[u16 len][text]` 计数串），仍被还原成日文，v1.18 的存档备注修复不受影响。
重建后回味正文显示中文。

## 三、未解决：极短的特殊样式台词在回味里仍是日文

### 现象
回味里唯独 `【まりか】「えい！」`（21 字节，msglog hit 163；同句在 hit 120 也出现过）
显示为**日文**，且该行字体比其它行小、带半透明柔光；滚动时它随列表移动，说明确实是列表中的一条。

### 取证结论
1. 全地址空间扫描（812 个可读区、297MB）：完整 JP 串 **0 份**（SJIS 与 UTF-16 都查过），
   只有 CN 拷贝 4 份（0x3fd6ce9 池、0x11a53b98、0x11a59f98、0x11a5ad38）。
2. 对照实验：把当前正常显示中文的 `【正樹】「なんだよ？」` 的唯一 CN 拷贝（0x11a58450）改成 `【Q島】…`，
   重开回味后**该行仍显示原文**（截图逐字节相同）→ 回味渲染**不回读这些文本缓冲**。
3. 结论：这条是**预先栅格化的文本图（text image）**，在文本被替换之前用日文生成；
   普通行则由引擎按中文实时重绘，所以只有这一条留在日文。
4. 之前误判的「JP 拷贝在 0x5ace8ba0」其实是**本补丁 DLL 自己的静态缓冲 `g_orig`**（存最近一次替换前的日文原串），
   不是引擎的副本，所以当时打标记无效。

### 影响与建议
- 只影响「特殊样式（小字号/半透明）」台词的**回味**显示，正文、存档、选项均正常。
- 彻底修复需要逆出「文本图」的生成点（在脚本入队阶段把日文栅格化），属于较大的逆向工程；
  或改走「脚本池预翻译」路线（在显示前把脚本原文池改写成中文），但那会把存档备注一起变成中文，
  与 v1.18 的备注修复冲突，需要同时改造备注还原逻辑。

## 四、当前产物
- cnpatch.dll（tunnel 修复 + namemap 重生成 + keepzh），sha256 `eafcadd8da31ab676c927919cb58225ef705216d42d62083382f16ebd14bda27`（109,285 B）
- 构建命令（必须带 hookstub3.S，且 TMPDIR 需改到可写目录）：
  ```
  export TMPDIR=$PWD/build_tmp TMP=$PWD/build_tmp TEMP=$PWD/build_tmp
  tools/mingw/mingw32/bin/i686-w64-mingw32-gcc.exe -O2 -shared -o build_tmp/cnpatch_new.dll \
    cnpatch.c choice.c hookstub2.S hookstub3.S -lgdi32 -luser32 -ladvapi32 -Wl,--enable-stdcall-fixup
  ```
