# H3BattleStore — 战场存档

《英雄无敌 III》SoD HD Mod 插件。在手动战斗中保存当前战斗时刻，并在同一场战斗里恢复到该时刻。

> 当前状态：正式代码、文件存储和 UI 已实现；Release|Win32 全量编译及文件层测试已通过。真实战斗中的完整恢复和无崩溃动态验收仍需在目标游戏环境中完成，未完成前不应视为最终发布版。

## 功能

- 在战斗空闲、等待玩家操作时保存当前时刻，而不是只保存战斗开始状态。
- 同一场战斗支持多条存档，列表按时间倒序显示。
- 保存和恢复战场单位的损血、数量、站位、行动状态、法术持续、英雄战斗副本魔力、障碍、城墙、战斗日志及单机随机数等状态。
- 悬浮条显示在战场画面上方，不占用战场格子。
- 点击悬浮条展开当前战斗的存档列表；读档前会确认。
- 右键删除存档。
- 拖动悬浮条改变位置，位置写入玩家配置并在后续战斗中保留。
- 点击悬浮条右侧的快捷键名称可以改键，默认存档键为 `Z`。
- 存档文件独立于游戏战略地图存档，不会出现在原版读档列表中。
- 每场最多保留 30 条记录，磁盘最多保留最近 30 场战斗。

## 安装与部署

需要 32 位 Heroes III SoD 和 HD Mod 插件环境。

将以下文件放入：

```text
<游戏目录>\_HD3_Data\Packs\战场存档\
├── H3BattleStore.dll
├── H3BattleStore.default.ini
└── 使用说明.txt
```

然后打开 HD Mod 启动器，在插件列表中启用「战场存档」。

当前项目的部署脚本使用以下游戏目录：

```text
D:\Heroes3\Heroes3_2026.05.01\_HD3_Data\Packs\战场存档\
```

可执行：

```powershell
.\build.ps1
.\deploy.ps1
```

或者使用 `build_and_deploy.bat` 一次完成编译和部署。

## 使用方法

1. 进入手动战斗后，战场上方会出现悬浮条。
2. 按悬浮条显示的快捷键保存当前战斗时刻；保存成功时会短暂显示时间反馈。
3. 点击悬浮条左侧区域展开列表，点击某条存档并确认即可恢复。
4. 右键存档条目可以删除它。
5. 按住悬浮条空白处拖动，松手后保存新位置。
6. 点击右侧快捷键名称，按允许的字母改键；按 `Esc` 取消改键。

插件只在手动战斗中显示悬浮条。动画、敌方行动、自动战斗、结算和结果界面等不安全时机不会执行保存或恢复。

## 存档与配置文件

运行时文件位于插件目录：

```text
H3BattleStore.data/       战斗存档数据
H3BattleStore.user.ini    玩家配置（首次使用时可不存在）
H3BattleStore_*.log       插件日志
```

`H3BattleStore.default.ini` 是随插件分发的默认配置，升级时可能被覆盖；玩家改动应写在 `H3BattleStore.user.ini`。

```ini
[Logging]
DisableLog=0
MinLevel=info

[Ui]
BarX=16
BarY=4

[Hotkeys]
SaveKey=Z
```

## 构建

目标为 `Release|Win32`。使用 Visual Studio MSBuild：

```powershell
& "C:\Program Files\Microsoft Visual Studio\18\Enterprise\MSBuild\Current\Bin\MSBuild.exe" `
  H3BattleStore.vcxproj /t:Rebuild /p:Configuration=Release /p:Platform=Win32
```

输出文件：

```text
Release\H3BattleStore.dll
```

## 测试

文件格式、编解码、CRC、原子写入、删除安全检查和存档保留策略测试：

```powershell
.\tests\run-archive-tests.ps1
```

当前已验证：

- Release|Win32 全量 Rebuild：0 error / 0 warning。
- Archive/Codec 文件层测试：全部通过。

尚未替代动态验收的内容：真实战斗中恢复后的损血、站位、魔力、等待队列、法术、战争机器、召唤/镜像、障碍、城墙、日志和随机序列一致性，以及恢复过程无崩溃。

## 设计与逆向资料

- [需求文档](需求文档.md)：玩家行为和验收标准。
- [设计文档](设计文档.md)：快照、恢复安全窗口、文件格式和 Hook 设计。
- [使用说明](使用说明.txt)：面向玩家的安装和操作说明。
- `H3Note\战斗中存档可行性逆向笔记.md`：结构、地址和恢复算法的静态逆向依据。

## 兼容边界

- 目标版本是 Heroes III SoD 32 位 x86；硬编码地址针对目标 SoD EXE。
- 不修改原版战略地图存档系统。
- 不依赖 ERA、ZCN2.dll、H3.TextColor.dll 或其它插件。
- 与打铁助手、远优对比、人性化读档可同时加载，但本插件不读写打铁助手的方案存档。
