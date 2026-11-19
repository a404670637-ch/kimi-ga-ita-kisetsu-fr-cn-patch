# v1.20-dev：移除 A 路径的 Unicode 键回退（待实机 A/B）

## 改动
`cnpatch.c` → `my_GetGlyphOutlineA`：

```c
// 旧
if (find_uni((unsigned short)ch, &uni)) { hit = 1; }
else if (g_nrmap && find_rev(ch, &uni)) { hit = 2; }   // ← 删除
```

A 入口收到的是**原始 SJIS 码**，而 `g_rmap` 的键是 `MultiByteToWideChar(932, 隧道码)` 得到的 **Unicode**，
两者值域重叠 → 原生字符被误画成某个中文（就是 v1.19 修掉的 【６りか】/ま→６ 那一类）。
W 入口（`my_GetGlyphOutlineW`）保留 `find_rev`，因为那里收到的确实是 Unicode。

## 静态验证（`work/p5/t_verify_v120.py`）
- 翻译表里出现的 CJK 码：**隧道 3,089 / 原生 119**；
- 旧逻辑下这 119 个原生码被误画的个数：**0**（所以改动对已发布译文是 no-op）；
- 非语料文本（UI/菜单/DLC HUD）里仍存在的误画配对：**159 组**，改动后全部消失；
- 构建产物 `cnpatch_v120.dll`（109,285 B，sha256 `1e29acaaeba5eb12f8c64c10ab407b808385e2b1c2fe47bb736afb44127fb819`），
  二进制里 `RA` 日志标记已消失、`RW` 保留，可确认改动生效。

## 待做的实机 A/B（重启游戏后 5 分钟）
1. 备份游戏目录 `cnpatch/cnpatch.dll`；
2. 用 `cnpatch_v120.dll` 覆盖；
3. 启动 → 读档 → 连续推进 30 句，确认正文中文正常、无乱码/无缺字、字色正常；
4. 打开回味，确认各句中文（除已知特殊样式行）；
5. 打开右键菜单进设置再退出，确认 UI 没有出现新的错字（这一步才真正验证 159 组的修复）；
6. 若出现任何正文异常，立即换回备份 DLL。
