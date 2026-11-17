# Hyprland IME fullscreen repair 1.0.0

修复 Hyprland 0.56.2 中 foot 等原生 Wayland 应用真正全屏后，输入法候选窗消失的问题。窗口继续保持 `fullscreen=2`、`fullscreenClient=2`，不修改 Super+F，也不显示顶部栏。

这是加载到 Hyprland 的可选插件，与本仓库的 Fcitx Sumi 候选桥接插件独立。无需替换或重启 Fcitx，不包含输入法引擎或词库。

## 兼容性

已验证环境：Arch Linux / Omarchy，x86_64，Hyprland `0.56.2-4`，提交 `efb50993780079460b0cbed1363e2166a2de1d9f`，Fcitx `5.1.23`，foot 原生 Wayland。

预编译文件的 Hyprland ABI：

```text
efb50993780079460b0cbed1363e2166a2de1d9f_aq_0.15_hu_0.14_hg_0.5_hc_0.1_hlg_0.6
```

构建使用 GCC 16.2.1、Aquamarine 0.15.1、Hyprutils 0.14.2、Hyprgraphics 0.5.1、Hyprcursor 0.1.13、Hyprlang 0.6.8。预编译包仅用于匹配环境；其他环境应使用对应的 Hyprland 开发头文件本地编译。插件加载时校验 ABI，不匹配会拒绝加载。Hyprland 升级后需要重新检查兼容性，不能直接沿用旧二进制。

## 安装

下载并校验 Release 附件，解压二进制包后进入其目录：

```sh
./install.sh
hyprctl plugin load "$HOME/.local/share/hyprland/plugins/ime-fullscreen/ime-fullscreen.so"
hyprctl plugin list
```

安装脚本仅将插件和构建资料安装到当前用户的数据目录，不修改桌面配置。设置了 `XDG_DATA_HOME` 时，安装路径使用该目录；相应调整加载路径。

Omarchy Lua 配置可在 `~/.config/hypr/hyprland.lua` 中增加一行，实现自动加载：

```lua
hl.plugin.load(os.getenv("HOME") .. "/.local/share/hyprland/plugins/ime-fullscreen/ime-fullscreen.so")
```

然后执行 `hyprctl reload` 和 `hyprctl configerrors`。已加载插件无需再次手动加载。当前版本不需要注销或重启桌面。

从源码构建需要与运行版本匹配的 Hyprland 头文件、GCC 和 pkg-config：

```sh
./build.sh
./install.sh
```

## 原理与影响

Hyprland 的 solitary fullscreen 优化只绘制全屏应用主表面，跳过 `renderIME()`。本插件在公开的 `render.preChecks` 事件中检查当前聚焦显示器；当输入法已连接、聚焦的原生文本输入已启用时，取消该帧的 solitary 快速路径，让正常渲染流程绘制候选窗。处理发生在直接扫描输出和 tearing 检查之前，不修改窗口的全屏状态。

取舍：原生文本输入启用期间（包括两次组字之间）暂时不使用 solitary 优化。其他显示器和未启用原生文本输入的全屏应用保留原路径。插件不增加持续重绘循环、不替换内部函数，也不读取输入文字。

上游对应问题：[Hyprland #16010](https://github.com/hyprwm/Hyprland/issues/16010)。

## 验证

2026-11-17 使用私有 D-Bus、独立 Fcitx 和嵌套 Hyprland 的 headless 输出完成对照验证：

- 普通窗口显示候选；真正全屏且不加载插件时候选消失。
- 同一全屏窗口加载插件后恢复；卸载后再次消失；重新加载再次恢复。
- 空格选词正常提交“你好”，全过程全屏状态保持 `2/2`。
- 当前真实桌面的 foot 真正全屏截图确认候选可见；用户确认输入正常。

`verification/` 保存了仅包含测试终端的前后截图与验证摘要。隔离测试需要关闭 `misc.disable_watchdog_warning` 对应的启动警告显示（设置该项为 `true`），否则警告浮层本身会阻止 solitary 优化，从而掩盖缺陷。

## 卸载

删除配置中的自动加载行，执行 `hyprctl reload`、`hyprctl configerrors`。若此前仅通过命令加载，使用：

```sh
hyprctl plugin unload "$HOME/.local/share/hyprland/plugins/ime-fullscreen/ime-fullscreen.so"
```

插件卸载后可删除其安装目录。配置恢复原状，真正全屏快捷键始终不变。
