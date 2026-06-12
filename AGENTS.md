# AGENTS.md

本文件为参与本项目的编码代理提供协作约定。

## 项目

`Calendar_epdiy_v7` 是一个基于 ESP-IDF 的 ESP32-S3 项目，用于驱动 epdiy 电子纸日历。

当前硬件默认值定义在 `main/app_config.h`：

- 开发板：`epd_board_epdiy2_s3`
- 屏幕：`ED060KD1`
- 目标芯片：`esp32s3`
- 已验证的 ESP-IDF 版本：`v5.5.4`

## ADC 按键

开发板有 3 个 ADC 按键，三个按键共用同一个 ADC 输入：

- 按键 ADC IO：`GPIO19`
- ESP32-S3 ADC 映射：`ADC2 channel 8`
- 未按键原始值：`4095`
- 三个按键原始值：`970`、`1993`、`2775`

按键阈值：

```text
raw < 1400        key 3
1400..2399        key 2
2400..3399        key 1
raw >= 3600       no key
```

注意：`GPIO19`/`GPIO20` 在 ESP32-S3 上常作为原生 USB D-/D+。本项目当前通过 UART/CH340 串口烧录和 monitor；如果以后启用原生 USB console/JTAG，要先评估它和 `GPIO19` ADC 按键线路的冲突。

## 环境

在仓库根目录运行命令。编码代理的当前工作目录也应保持为仓库根目录。

如果 `idf.py` 不在 `PATH` 中，请先使用当前主机平台对应的 ESP-IDF 环境初始化方式。

常用命令：

```sh
idf.py set-target esp32s3
idf.py build
idf.py flash
idf.py monitor
```

本项目已经使用 ESP-IDF v5.5.4 成功编译和烧录。

## 代码布局

- `main/`：应用入口和日历应用代码。
- `components/epdiy2/`：vendored epdiy 驱动。
- `build/`、`sdkconfig`、`dependencies.lock`：ESP-IDF 生成或维护的文件。

应用代码应放在 `main/`，或放在项目自有的新组件中，例如：

```text
components/calendar_ui/
components/time_sync/
components/storage/
components/network/
```

不要把应用代码放进 `components/epdiy2/src/`。除非任务明确要求修补驱动，否则把 `components/epdiy2/` 视为第三方驱动代码。

## 渲染验证 Harness

`harness/` 是独立的主机端 CMake 项目，无需烧录即可在电脑上渲染出图：

```sh
./harness/run.sh
open harness/out/render.png
```

它和设备构建编译**同一套**渲染源文件（`main/` 渲染模块 + epdiy 的
`epdiy.c`/`font.c`/`displays.c`/`builtin_waveforms.c`），只 stub 硬件层。
修改渲染、viewport、字体相关代码后，除了 `idf.py build`，也应运行
`ctest --test-dir harness/build` 并查看输出 PNG 验证效果。
详见 `harness/README.md`。

**渲染代码位置约束**：`main.c` 只负责系统初始化与任务编排（FreeRTOS 任务、
按键、SD 挂载），不得直接出现渲染调用——包括 `epd_draw_*`、`epd_write_*`、
`epd_fill_*`、`epd_hl_*`、`viewport_*`、`display_font_draw_text` 以及
`epd_init`/`epd_set_rotation` 等显示初始化。渲染的初始化、绘制与上屏必须
通过 `display_screen.h` 的共享入口完成：

- `display_render_init()`：显示栈初始化（epd_init、VCOM、旋转、viewport），
  返回 highlevel state。
- `display_draw_sd_screen()` 等绘制函数：所有画面绘制。
- `display_present()`：上电、刷屏、断电（harness 上的语义是写出 PNG）。

新增渲染逻辑必须写在 harness 共同编译的共享文件里（`display_*.c`、
`text_utils.c` 或新的共享模块，并同步加入 `harness/CMakeLists.txt`），
否则它不会被 harness 覆盖，电脑上的渲染结果将不再等于设备结果。
harness 入口 `harness/src/harness_main.c` 与 `main.c` 必须调用相同的
共享入口，不要在两边各自复刻渲染流程。

## 四边遮挡与 viewport

这个电子纸屏幕可能会放入相框，屏幕四边可能被遮挡不同长度。应用代码使用 viewport 抽象处理这个问题。

四边遮挡配置定义在 `main/app_config.h`：

```c
#define DISPLAY_OCCLUDE_LEFT 0
#define DISPLAY_OCCLUDE_TOP 0
#define DISPLAY_OCCLUDE_RIGHT 0
#define DISPLAY_OCCLUDE_BOTTOM 0
```

这些值的单位是像素，方向以 `epd_set_rotation(EPD_ROT_PORTRAIT)` 之后的屏幕方向为准。也就是说，`LEFT`、`TOP`、`RIGHT`、`BOTTOM` 表示最终可见画面方向下的四边，而不是裸屏未旋转方向。

viewport 的实现位于：

```text
main/display_viewport.h
main/display_viewport.c
```

绘制业务代码应把 viewport 当作可见屏幕：

- `viewport_width()` 返回遮挡后的可见宽度。
- `viewport_height()` 返回遮挡后的可见高度。
- `viewport_write_string()` 使用 viewport 坐标绘制 epdiy 内置字体。
- `viewport_draw_text()` 使用 viewport 坐标绘制 SD/FreeType 字体。
- `viewport_fill_rect()` 和 `viewport_draw_pixel()` 使用 viewport 坐标，并裁剪到 viewport 内。

布局代码不要直接使用 `epd_rotated_display_width()` 或 `epd_rotated_display_height()` 作为业务布局尺寸；应使用 `viewport_width()` 和 `viewport_height()`。

绘制代码不要直接调用 `epd_write_string()` 或 `display_font_draw_text()`，除非是在 viewport wrapper 内部。业务代码应通过 `viewport_write_string()` 和 `viewport_draw_text()` 绘制文字。

`DISPLAY_MARGIN_X` 和 `DISPLAY_MARGIN_Y` 的语义是 viewport 内部边距，不是裸屏物理边距。实际物理偏移由 viewport 统一加上四边遮挡值。

如果修改四边遮挡、屏幕旋转、屏幕型号、VCOM 或 PSRAM 相关配置，修改后运行：

```sh
idf.py build
```

如果需要让设备立即使用新遮挡参数，还需要重新烧录：

```sh
idf.py flash
```

## 编辑规则

- 优先做小而聚焦的修改。
- 除非配置变更是任务目标，否则不要把 ESP-IDF 生成的输出纳入源码修改。
- 不要手动编辑 `build/` 产物。
- 新源文件默认使用 ASCII；只有在明确需要中文或其他 Unicode 内容时才使用非 ASCII。
- 修改开发板、屏幕、VCOM、旋转、PSRAM 或四边遮挡/viewport 相关配置后，运行 `idf.py build`。

## 已知警告

`main/firasans_20.h` 可能触发 GCC `-Wbidi-chars` 警告，因为它是从 epdiy demo 复制来的生成字体头文件。这个警告目前不阻塞编译。

如果警告变得过于嘈杂，优先考虑重新生成或隔离字体资源，而不是全局关闭警告。

## 运行时调试记录

添加 SD 卡 FreeType 字体渲染时得到的经验：

- FreeType 渲染比原先静态字体日历路径使用更多栈空间。从 `/sdcard/fonts` 渲染字体时，保持 `CONFIG_ESP_MAIN_TASK_STACK_SIZE` 至少为 `24576`。
- 主任务栈过小不一定会触发干净的 FreeRTOS 栈溢出 panic；它可能先破坏附近全局变量，然后以误导性的 `LoadProhibited` 崩溃。
- 曾观察到的栈破坏特征：日志出现 `display_font: loaded SD font: ...` 后，在 `rasterize_glyph()` 或 `display_font_text_width()` 崩溃，`EXCVADDR: 0x00000154`，全局 FreeType face 指针看起来被改成 `0x100`。遇到这种情况时，先检查栈大小，不要先假设字体文件或 SD 挂载失败。
- 在 `idf_monitor` 外捕获到回溯地址时，使用：

```sh
xtensa-esp32s3-elf-addr2line -pfiaC -e build/Calendar_epdiy_v7.elf <addr...>
```

- SD 卡应保持挂载，直到所有 FreeType 绘制完成并且动态字体完成 deinit。FreeType 在栅格化 glyph 时可能仍需要访问字体文件。
- SD 字体支持需要保持 FatFs 长文件名和 UTF-8 API 编码开启；中文字体名和 `.txt` 内容依赖这些配置。预期字体位置是 `/sdcard/fonts/<first .ttf or .otf>`。
- `epdiy` 关于 32 字节 cache line 的警告会导致驱动把 pixel clock 从 20 MHz 降到 10 MHz。这个警告很吵，但不是之前 SD 字体故障中的重启原因。

## 烧录记录

项目已经成功烧录到带 8MB embedded PSRAM 的 ESP32-S3。

优先使用 ESP-IDF 自动检测串口。如果自动检测失败，使用主机平台的常规方式列出串口，并显式传入所选端口：

```sh
idf.py -p <port> flash monitor
```

Windows 下，如果控制台使用 GBK，`idf.py monitor` 可能会因为 ROM boot 字节解码成替换字符而在打印有用日志前失败。监视串口时使用 UTF-8 环境：

```powershell
[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false)
$env:PYTHONIOENCODING = 'utf-8'
$env:PYTHONUTF8 = '1'
idf.py -p COM4 monitor
```

如果一次超时的 monitor 命令之后 `COM4` 或其他串口被拒绝访问，检查是否有残留的 ESP-IDF Python monitor 进程，并只停止当前调试会话创建的进程。

如果旧固件重启期间自动 reset 失败，重试：

```sh
idf.py -p <port> flash
```

如果仍然无法连接，手动进入下载模式：按住 BOOT，点按 RESET，然后松开 BOOT，再执行烧录。
