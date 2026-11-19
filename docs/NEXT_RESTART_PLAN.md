# 下次重启时要做的事（一次性，约 5 分钟）

> 前提：用户同意重启游戏（当前这段进度没有存档）。重启前先做存档目录快照，测试完再还原。

## 0. 快照（重启前，自动）
```
work/p5/t_save_snapshot.py   # 复制 Vmreg 到 savebak7_<时间> 并生成 MANIFEST.sha256
```

## 1. 部署 v1.20 候选（可选，A/B 用）
game/cnpatch/cnpatch.dll  ←  work/p5/build_tmp/cnpatch_v120.dll（sha256 1e29acaa…）
先备份原 DLL；若正文出现异常，立刻换回。

## 2. 打开字体诊断
游戏目录 cnpatch/config.ini 追加一行：`fontdiag=1`（保持 enabled=1 / storefix=1 / keepzh=1）。
启动游戏（Kimiita.exe，弹窗点「忽略」/「否」），等到标题 → 读档 → 进入正文。

## 3. 抓「回味到底怎么画字」
1. 让画面上出现一句带名牌的台词（任意）；
2. 右键 → バックログ（屏幕 (507,461)），停留 3 秒；
3. ESC 退出，再推进 2~3 句；
4. 运行：
   ```
   work/p5/t_fontdiag_analyze.py
   ```
   它会读 `cnpatch/fontlog.txt`，找出假名序列 `82DC 82E8 82A9`（まりか）等，
   并打印它们的**调用方返回地址**与出现次数。

## 4. 判读
- 若日志里出现回味那几行的假名码，且调用方地址集中在某一段（rvmm/UnivUI/Vm60 之一）
  → 那就是「整行文字渲染层」，可据此下钩子；同时说明回味是**实时读文本**的，
  之前的「改内存无效」另有原因（很可能是读取的副本我们没找对）。
- 若日志里**完全没有**回味那几行的假名码 → 回味确实是**预渲染/贴图**，
  只能从资源层解决（换图），或接受为限制。

## 5. 收尾
- 关游戏（**不要**用「終了」正常退出前，先确认是否需要保留已读标记；按用户规则：快进过就只许 taskkill /F）；
- 还原 Vmreg：`work/p5/t_save_restore.py` 按 MANIFEST 校验；
- `fontdiag` 改回 0（或删掉该行）。

## 附：v1.21-diag 诊断 DLL（已构建，2026-11-20 06:30）

`work/p5/build_tmp/cnpatch_diag.dll`（110,360 B，sha256 `c1d6d7ad68107b2cdb17cac131c5e66b69d71bd1b95e3609abe473418b294228`）

在 v1.20 候选（移除 A 路径 Unicode 键回退）基础上增加了：
- 维护最近 8 个字形码的滑窗；
- 当滑窗命中 `【まりか`(8179 82DC 82E8 82A9) 或 `【万里花`(8179 969C 97A2 89D4) 时，
  用 `CaptureStackBackTrace(1,10,...)` 抓一次调用栈，写入 `fontlog.txt` 的 `STACK` 行（最多 40 次）。

判读：`STACK ... n=.. AAAAAAAA BBBBBBBB ...` 里
- 第 1 帧恒为 UnivUI 的字形包装函数（RVA 0x307A0 附近调 GetCachedFont 的那唯一一处，见下）；
- 第 2~4 帧才是**上层排版/绘制函数**——它所在的模块与 RVA 就是答案：
  - 若集中在 rvmm/UnivUI 的同一处 → 那就是整行渲染层，可以下钩子；
  - 若栈里根本没有这些帧 → 说明回味不经过 GetCachedFont（预渲染/贴图）。

## 附二：静态定位到的唯一字形入口（本轮 RE 成果）

- `CS5RFont::GetCachedFont` = UnivUI+0x2F250（序数 472，运行时 VA 0x31FF250，
  序言 `6A FF 68 A8 8E 21 03` 已验证）；
- 在 UnivUI 的 .text 里**只有一处**直接 `call` 它：RVA **0x307FB**（VA 0x32007FB），
  位于 RVA 0x307A0 起的一个 SEH 包装函数内（`push ecx; mov eax,esp; ... call 0x31D17D0;
  push ecx; mov ecx,esi; call 0x3202A70; mov ecx,eax; call GetCachedFont; ret 4`）；
- 该包装函数在 dump 里既不在导出表里，也没有任何指针/相对调用引用它
  （引擎的跨模块调用方式待查，可能是运行时构造的跳转表），所以「谁调用了它」只能靠**栈回溯**回答。
