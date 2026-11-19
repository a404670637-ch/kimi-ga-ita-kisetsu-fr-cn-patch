# 存档界面文字乱码：机制与结论（2026-11-02）

## 1. 现象
存档卡片里的「コメント」显示为随机日文汉字（例：2026/10/30 18:20 存档显示「冩盆暮偰偲」）。
存档标题、日期、标签（記録日／コメント／セーブメニュー）都正常。

## 2. 结论先行
- **引擎的 UI 文字（标题菜单、存/读档卡片）不是 GDI 画出来的**，走的是 rvmm/UnivUI 自带的「文本图像」管线；
  它按 SJIS 码去字体缓存取字形，**隧道码（0x989F–0x9FFD / 0xE040–0xEAA5）在这套字体里对应的是无关的日文汉字**，
  所以汉化文本一旦进存档，读档界面就成了乱码。
- 正文/选项能显示中文，是因为它们走的是另一条路：引擎逐字调用 GDI `GetGlyphOutlineA` 取轮廓再自己 blit，
  我们的字形钩子（换成 SimSun 取中文字形）在这条路上有效。
- **存档里保存的是中文（隧道码）原文**：patch 在 0x79B55 处把引擎的消息缓冲区原地改写成中文，
  引擎随后把这段中文复制进自己的文本池（槽结构：[flags=1][len][cap=0x80][SJIS 文本]），
  存档 comment 就来自这份拷贝。因此读档界面拿到的是中文隧道码 → 渲染成随机汉字。

## 3. 本轮实测证据
1. **GDI 钩子全空**：对 rugp.exe / Vm60 / UnivUI / rvmm / GMfc / MFC42 的
   TextOutA/W、ExtTextOutA/W、DrawTextA/Ex、GetGlyphOutlineA/W 全部挂 IAT 钩并在标题+读档界面复现，
   trace.log / glyphlog.txt **一条都没有**（代码在，确实没被调用）。
2. rvmm 的字体相关字符串：`CTextImage`、`CLayoutTextImg`、`テキスト画像;rti`、`strFontCachePath`、`strStandardFont`、
   `strInstallFontFolder`、`nFontBlank` → UI 文本是「文本图像」管线（软件渲染/字体缓存），不是即时 GDI 绘制。
3. UnivUI 导出 `CS5RFont::GetCachedFont(uChar, node)`（mangled 名，另有序数 472）确实是 UI 取字形的入口：
   把该函数 inline hook 后，读档界面渲染日期时抓到 `uChar=0x0032 ('2')` —— 说明日期/卡片文字都走这个字体缓存。
   但**我们的 trampoline 破坏了函数序言，游戏当场崩溃**（dump 已归档），需要正规 detour 才能继续用它。
4. 指针旁路实验：把中文写进独立缓冲区、只把绘制指针换过去（引擎自己的缓冲区保持日文）——
   画面照样是中文，但内存里引擎文本池的两个槽（0x…43080 / 0x…43230，len=中文长度、cap=0x80）
   **仍然被复制成中文**，说明引擎在绘制时把传入字符串拷进了自己的文本池，换指针没用。
5. 手工把上述两个池槽改回日文（len 44→49，cap 足够）后重开保存界面：预览的 comment 仍然乱码，
   说明保存界面读的是**另一份拷贝**（扫描时还有第三处 0x3FExxxxx 的中文串）。

## 4. 可行的修复路线（按代价排序）
- **路线 A（彻底、代价最大）**：正规 detour `CS5RFont::GetCachedFont` 与 `CS5i::DrawFont`，
  在字形层把隧道码替换成中文位图/或用引擎自己的 `CreateNewFont` 造字形。
  参考开源实现：https://github.com/cssxsh/rUGP-Fuck （同样 detour 这两个函数，用 Microsoft Detours；
  该项目的 HookGetCachedFont 对 `uChar > 0xFFFF` 会调 `CreateNewFont`，说明引擎支持动态造字形）。
  需要先拿到这两个函数的可靠序言（可用 Detours 库或自写 5 字节 trampoline），再把 tunnel→中文 的映射接进去。
- **路线 B（绕开）**：让存进存档的 comment 变回日文。需要在存档序列化前把引擎文本池里的中文拷贝还原成日文；
  本轮只定位到 3 处拷贝中的 2 处，且保存界面读的不是这两处，还需继续找「保存界面读的那份」。
- **路线 C（不改）**：在说明文档里注明「存档 comment 在中文模式下可能显示为日文乱码，属于 UI 字体限制」。

## 5. 目前工程状态
- **游戏目录部署的是生产版** `cnpatch.dll`（sha256 2f7f27be…，87,438 B，v1.7-dev 含 msglog），config 已还原
  （enabled=1/log=1/hotkey=0x77/misslog=0）。
- 源码 `work/p5/cnpatch.c` 比生产版多了这些实验性代码（默认全部不启用，可安全保留）：
  DrawTextA/DrawTextExA 替换钩子、GetGlyphOutlineW 钩子 + CP932 反查表（`build_rmap`）、
  `fontdiag` 配置开关控制的 CS5RFont 诊断钩子（`hookstub3.S`）、glyphlog/trace=3。
- 设备：`work/p5/t_zhfind.py`（在进程内存里找中文串）、`t_zhpatch.py`（把池槽改回日文）、
  `t_tunscan.py`（隧道码串扫描）、`t_imp2.py`（从内存镜像解析 PE 导入表）、`t_adv.py`（点击推进）、
  `hookstub3.S`（GetCachedFont 诊断 stub，需修 trampoline 才能再用）。
- 存档目录保持原样（11 个 vmr*.uuc），实验时新建的 vmr00005 已删除；
  实验崩溃 dump 已移到 `work/p5/crashdumps_20261101/`。
## 6. 路线 A 第一轮实测（2026-11-02 04:20–04:45）
1. **CS5RFont::GetCachedFont 的地址与序言（已抓到）**
   - UnivUI.dll 只导出 1 个具名导出（PluginThisLibrary）+ 989 个**序数**导出；GetCachedFont = **序数 472**，RVA **0x2F250**。
   - 真实序言：`6A FF 68 <SEH handler> 64 A1 00 00 00 00 ...`，即 push -1（2 字节）+ push imm32（5 字节）= **第一条完整指令组 7 字节**。
     上一轮 5 字节 detour 把 push imm32 切断 → 崩溃；改成「复制 7 字节、跳回 body+7」后**不再崩溃**。
2. **UI 文字确实是逐字问字体缓存**：读档界面渲染时 fontlog 抓到
   `0032 0030 0032 0036 ...`（日期「2026/09/02 …」）以及 `8179 90B3 8EF7 817A …`（SJIS 的【正樹】），
   并且**有 12 条 TUNNEL 记录**（996F 98B2 99D3 9970 99D8 98DB ×2）——正是那张 10/30 存档中文备注的 6 个汉字。
   → 结论：读档界面确实把隧道码交给 CS5RFont 取字形，只是拿到了日文字形。
3. **inline hook gdi32full!GetGlyphOutlineA 导出后，UI 渲染期间 0 次调用**
   - gdi32full 的导出序言是 `8B FF 55 8B EC`（正好 5 字节，5 字节 detour 安全），已成功挂上（日志 `gdi export hook: GetGlyphOutlineA at 76911C70 tramp 00AF0000`）。
   - 但读档界面绘制时 **glyphlog 一条没有** → UI 字形不是 GDI 生成的。
   - 引擎模块字符串里有 TTF 表名痕迹：Vm60（cmap×1、loca×5）、rvmm（loca×6）、GMfc/UnivUI（CMap×3）→ **引擎自带 TTF 解析/栅格化**，字形来自它自己读的字体文件。
4. **失败的实验（不要再犯）**：把 stub 改成「先 call trampoline 拿到返回的 entry 指针再记日志」会当场崩溃——
   `call` 多压了一层返回地址，函数尾部的 `ret 8` 会把参数和返回地址一起弹掉，栈错位。要观察返回值必须自己重建调用帧（pop 原始返回地址再 call）。
   崩溃 dump 已归档到 `work/p5/crashdumps_20261101/`。
5. **下一步（路线 A 继续）**：
   - 目标函数：CreateNewFont（GetCachedFont 内部的调用目标，候选 `UnivUI+0x1F3E0` / `+0x1EF830`，序言 `6A FF 64 A1...`），它负责「字符码 → 字形」。
   - 找到引擎 TTF 的字形查找（cmap 查询）：把隧道码映射到**中文 TTF 的字形**，或在建字形时改用 SimSun 的光栅化结果。
   - 另一种更省事的可能：让引擎加载的 UI 字体文件换成含简体的字体（若字体来自 Windows Fonts 目录/可配置路径）。6. **追加实测（04:33–04:40）**
   - 引擎用 `CreateFontIndirectA` 建了一个 **"MS UI Gothic" / h=10 / charset=128(SHIFTJIS)** 的字体（fontlog 抓到 2 次）→ UI 字体确实是 GDI 字体句柄，但字形不是通过 GetGlyphOutlineA 导出取的（A 导出钩子 0 次调用）。
   - 给 **gdi32full!GetGlyphOutlineW 导出**加钩子后游戏崩溃（ACCESS_VIOLATION，dump 已归档）→ 这条 W 导出不能这样挂，已回退。崩溃点记录在 kernel32+0x1EB0B（异常分发，不是真凶地址）。
   - 结论不变但更明确：字形来自引擎自己的 TTF 解析（Vm60 有 cmap/loca，rvmm 有 loca），下一步应直接找 `CreateNewFont`（候选 UnivUI+0x1F3E0 / +0x1EF830）里的「字符码 → 字形」查询点。
### 6.1 第二轮（04:39–04:45）：字形条目样本 + 调用帧坑
- 成功用「先 pop 原始返回地址、再 call trampoline」的方式**拿到了 GetCachedFont 的返回值**（entry 指针），但游戏在读档界面之前崩溃（ACCESS_VIOLATION，dump 已归档）。
  说明 GetCachedFont 的收尾不总是 `ret 8`（或存在不走同一返回路径的分支），这种「代调用」方式不稳定；**安全的做法仍是 tail-jump（只记字符码，不取返回值）**。
- 崩溃前拿到了一个字形条目样本（字符 '2'，entry=0x11C826CC）：
  `14 27 C8 11 42 00 32 08 0B 00 0B 09 00 01 02 65 7D 41 71 CD 01 5D 41 69 55 FE 01 ...`
  - +0x00 是个指针（0x11C82714 = entry+0x48），+0x04/+0x06 疑似宽高/步长，0x0E 起像**行程压缩的位图行数据**；
  - 条目大小约 **0x44 字节**，连续排布（下一条目在 +0x44）。
  → 若能把条目格式完全解开，就可以为隧道码生成「中文位图条目」并在 GetCachedFont 里替换返回，这是路线 A 剩下的核心工作。
- 当前部署：已回退生产版 `cnpatch.dll`（2f7f27be…），config 复原，实验 dump 归档到 `work/p5/crashdumps_20261101/`。
### 6.2 第三轮（04:45–04:52）：字体对象已定位，但条目格式仍未解开
- **CS5RFont 对象地址**：GetCachedFont 的 this 稳定为 **0x03D0A000**（每次运行不同，但同一次运行内所有字符一致）。
  - +0x00 vtable = 0x03406338（UnivUI .rdata）
  - +0x04 指向一个容器（本次 0x11922B4C），容器首项是对象自身地址
  - 容器内是 **0x2C 步长**的节点数组，节点里只有自引用指针，前 0x200 字节里没有字符码
- **返回值捕获实验（改写调用者返回地址）**：能拿到一次 ENTRY（字符 '2'，entry=0x1143B6CC，内容与上次样本一致），**但随即崩溃**。
  推测原因：_saved_ret 用全局变量保存原返回地址，嵌套调用/多线程重入时会被覆盖 → 跳错地址。这条路需要 TLS 才能真正安全。
- **字形条目结构（两次样本一致）**：14 B7 43 11 42 00 32 08 0B 00 0B 09 00 01 02 65 7D 41 71 CD 01 …
  +0x00 → entry+0x48、+0x04=0x42、+0x06=0x0832、0x0E 起像是行程压缩位图。
- **当前结论**：路线 A 需要解开「0x44 字节条目 + 行程压缩位图 + 字体对象缓存键值」三件事，短期无法安全完成；
  按用户事先授权（很难避免附带问题的话允许先回退然后暂停）**已回退并暂停**。
  机器已恢复生产版补丁，11 个存档未动，实验崩溃 dump 归档在 work/p5/crashdumps_20261101/。
### 6.3 第四轮（2026-11-03 03:20–04:02）：字体 family / 字体文件路线全部证伪
参考 Muv-Luv 汉化补丁的「改 CreateFontIndirectW 的 face name + 专用字体」做法，本作实测结论如下：
1. **hook CreateFontIndirectA 改 face name 无效**：把引擎请求的 「MS UI Gothic」 改成 SimSun / KaiTi，
   CFR 日志确认改写生效（HFONT 正常返回），但读档界面文字**逐像素完全不变**（截图 byte-identical）。
   → 引擎的 UI 字形不来自 GDI 字体对象。
2. **用 export 级 CreateFileA/W 钩子（跟 thunk 追到 kernelbase 实现，序言 8B FF 55 8B EC）抓到进程内所有字体相关文件打开**：
   - C:\Windows\Fonts\staticcache.dat（GDI 字体缓存）
   - C:\Windows\fonts\msyh.ttc（微软雅黑）
   - C:\Windows\fonts\HYZhongHeiTi-197.ttf（汉仪中黑体，本机装的字体）
   - C:\Users\...\君がいた季節FR\4D8699CB.eot（**临时文件，内容是 .rio.ici 的完整拷贝 6536B，md5 相同，用完即删**）
3. **把这些 .ttf/.ttc 的打开重定向到别的字体（simkai.ttf / msgothic.ttc，A/W 两种调用都做了）→ 读档界面文字仍然逐像素不变**。
   → 这些字体文件不是 UI 字形来源。
4. **进程内存里扫不到任何 TTF 镜像**（找 00 01 00 00 且 0x200 内出现 cmap/glyf/head/hhea/maxp/loca/name/post ≥4 个表标签 → 0 命中）。
5. **GetGlyphOutlineW 导出钩子这次能稳定安装（fontdiag=2），读档界面绘制时 0 次调用**（glyphlog 无 GW 记录）。
   → UI 字形既不走 GDI A，也不走 GDI W。

**结论**：本作（2011 rUGP 5.95.08K）的 UI 字形来自引擎内部的数据（极可能是 .rio 内的自有/位图字体资源，
「テキスト画像;rti」一类），不经过任何系统字体或 GDI 出口。要让读档界面显示中文，必须替换/补丁这个内部字体资源——
属于深层逆向，且 .rio 不允许改动。**字体路线（Muv-Luv 那套）在本作不可行。**

下一阶段改走路线 B：让存档里保存/显示**日文原文**（读档界面不再乱码，显示可读日文）。
机制线索：patch 在 0x79B55 原地改写引擎文本缓冲，引擎绘制时会把文本复制进自己的文本池（槽头 [flags][len][cap=0x80]），
存档 comment 来自这份副本；若在绘制后把池里的中文副本恢复成原日文，存档就会写日文。

## 路线 B：已实现并实机验证（v1.18，2026-11-03）

**结论：字体路线不可行；「绘制后把引擎内部文本副本还原成日文」可行且已生产化。**

### 机制（实测）
- 0x79B55 钩子原地改写引擎文本缓冲 → 引擎绘制时把文本复制进自己的池：
  - 一类槽：[flags][len][cap=0x80][text]（cap 足够时可写更长的日文并更新 len）；
  - 一类计数串：[u16 len][text]（例 0x4012033，len=44 含 \x01；容量未知，只能等长回写）。
- 存档 comment 就是从这里取的：把副本改回日文后，保存界面预览立刻显示日文。

### 实现（work/p5/cnpatch.c）
- 全局：g_storefix（config storefix，默认 1）、g_pat[4096]、g_patlen、g_zhlen。
- capture_text 命中替换时把 zh 去掉尾部 0x01 存进 g_pat（g_zhlen 保留含 0x01 的长度）。
- restore_text2 在把原缓冲 memcpy 回日文后调用 fix_stored_copy()：
  - 遍历 MEM_PRIVATE + PAGE_READWRITE/PAGE_EXECUTE_READWRITE + ≤16MB 区域；
  - memchr 找首字节 + memcmp 全串；
  - 命中分类：*(DWORD*)(p-8)==g_zhlen 且 cap>=origlen+1 → 完整日文 + NUL + 更新 len；
    *(u16*)(p-2)==g_zhlen → 截断日文（sjis_fit 保证 2 字节字符边界）+ 空格补满；
  - 每次 16–31ms（日志 "storefix: 3 copies in 16 ms"）。

### 端到端验证（2026-11-03）
1. 新开一局，显示中文行 → 打开保存界面 → 预览 comment 为日文
   「【まりか】「わ、私は……その………」」。
2. 保存 → 退出、重启进程 → 「続きから」读档界面：新存档卡片备注为**可读日文**；
   同页 10/30 18:20 的旧存档仍是「写盆暮偰偲」（旧存档已把乱码写进文件）。
3. 读档进游戏：正文中文、名牌中文；右键「回想」（backlog）各句仍为中文。
4. 存档目录整目录还原（13 个文件 sha256 与测试前一致），gdb.uuc = d1f11c59…（干净基准）。

### 附带发现
- 存档/读档界面的标题、按钮、标签仍是日文（引擎 UI 直接绘制，不在文本表内）。
- 游戏内右键菜单是「終了/コンフィグ/回想/既読/送り/ロード/セーブ」。
- 保存界面的卡片顺序与 vmr 文件名不是一一对应，误点会覆盖别的槽位——
  任何存档试验都先整目录备份 Vmreg，测完还原。


## 附：v1.19 引入 keepzh 之后，存档备注是否仍然安全（2026-11-20 06:20 实测）

背景：v1.18 修好存档备注靠的是 storefix —— 每句绘制后把引擎侧的文本副本还原成日文。
v1.19 为了修回味，新增 keepzh=1：**显示缓冲**与**每行显示节点（SBO）**不再还原。
于是必须确认：存档备注的来源不是这两者中的任何一个。

实测（进程 7092，当前句【啓介】「ああそうだ！」）：
1. 关保存界面时扫描该句副本：日文 2 份（0x3c55690、0x3c55840，计数串）、中文 1 份（0x11a58d68，SBO 节点）。
2. 打开保存界面（右键第 1 项，屏幕 (507,337)）后再扫一遍：**副本集合完全不变**，
   说明预览不是新分配缓冲，也没多出中文副本。
3. 把 SBO 节点里的中文整段改成 `ZZZZZZZZZZZZZZZZ` 再重开保存界面：
   **画面 0 px 变化** → 保存界面预览**不读那个中文节点**。
4. 结论：预览读的是 storefix 仍在还原的**计数串**（见本文档「路线 B」一节：0x4012033 那条，
   [u16 len][text] 计数串才是存档 comment 的来源），因此 **keepzh=1 不影响存档备注修复**。
   （实验后已把节点字节逐字节还原，并复核画面与基线一致。）
