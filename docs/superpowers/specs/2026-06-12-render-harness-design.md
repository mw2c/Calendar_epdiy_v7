# 渲染验证 Harness 设计

日期：2026-06-12
状态：已批准（方案：纯替身 stub）

## 目标

无需烧录开发板，在电脑（macOS 主机）上直接编译并运行与设备完全相同的应用渲染代码，
输出渲染完成的 PNG 图片，用于人工或自动验证布局、换行、viewport 遮挡等渲染结果。

## 范围决策

- 应用渲染代码（`main/` 下的 `display_screen.c`、`display_viewport.c`、
  `display_font.c`、`text_utils.c`）**原样编译，零修改**。
- epdiy 驱动**不编译其源码**，由主机端替身（stub）实现其 API。
  替身行为对照 `components/epdiy2/src/epdiy.c` 与 `font.c` 移植，
  保证 framebuffer 布局与旋转语义一致。
- `sd_content.c` 不编译；harness 入口自行从本地目录构造 `SdDisplayContent`。
- `main.c`、`button_input.c` 不编译（FreeRTOS/硬件按键与渲染无关）。

## 目录结构

```text
harness/
  CMakeLists.txt        # 独立主机 CMake 项目，与 ESP-IDF 构建无关
  run.sh                # 一键 build + run
  README.md
  src/
    harness_main.c      # 复刻 main.c 绘制流程的主机入口
    epd_stub.c          # epd_* / epd_hl_* 替身实现
    png_writer.c/.h     # 4bpp framebuffer -> PNG
    stb_image_write.h   # vendored 单头文件 PNG 编码器
    esp_shim/           # 假 ESP-IDF 头文件
      esp_log.h esp_heap_caps.h esp_memory_utils.h
      esp_attr.h esp_assert.h driver/gpio.h
  assets/sdcard/        # 模拟 SD 卡：content.txt + fonts/*.ttf（字体不入库）
  out/                  # 渲染输出（gitignore）
```

## 接口一致性

- app 代码 `#include <epdiy.h>` 解析到 `components/epdiy2/src/` 的**真实头文件**，
  函数签名与 `EpdFont`/`EpdRect`/`EpdFontProperties` 等结构体与设备 100% 一致。
- 替身只提供实现，不复制声明。
- `ED060KD1`（1448x1072）与 `epd_board_epdiy2_s3` 由替身给出同值定义。

## 替身（epd_stub.c）行为

- framebuffer：4bpp、每行 `width/2` 字节，nibble 排列与 epdiy 相同；
  `epd_hl_set_all_white` = 填充 `0xFF`。
- 旋转：实现 `epd_set_rotation` 与 `EPD_ROT_PORTRAIT` 坐标变换，
  与 `epdiy.c` 的 `_rotate()` 逐式一致。
- 绘图：`epd_draw_pixel`、`epd_fill_rect`、`epd_draw_hline/vline`。
- 内置字体文字：`epd_get_text_bounds`、`epd_write_string`、
  `epd_font_properties_default`；unicode interval 查找 + glyph zlib 解压
  （FiraSans 数据为 zlib 流，用系统 zlib 的 inflate）+ 4bpp blit，
  对照 `font.c` 移植。
- `epd_hl_update_screen()`：主机语义 = 按当前旋转方向把 framebuffer 导出为
  PNG（PORTRAIT 下输出 1072x1448），灰度 4bpp 扩展为 8bpp。
- 硬件相关函数（`epd_poweron/poweroff/clear/ambient_temperature` 等）为 no-op
  或返回固定值（温度 25C）。

## 入口流程（harness_main.c）

1. 解析 CLI：`render_harness [--sdcard <dir>] [--out <png>]`，
   默认 `harness/assets/sdcard`、`harness/out/render.png`。
2. `epd_init` → `epd_set_rotation(EPD_ROT_PORTRAIT)` → `viewport_init()`
   → `epd_hl_init`（与 `main.c` 相同次序）。
3. 从 sdcard 目录读取第一个 `.txt` 填充 `SdDisplayContent.text`，
   找到第一个 `.ttf/.otf` 填充 `font_path/font_name`。
4. `display_font_init`（走 app 自己的 FreeType 代码，链接 Homebrew FreeType）。
5. `display_draw_sd_screen(&hl, &content, font)` —— 与设备同一函数。
6. `epd_hl_update_screen` → 写 PNG，打印输出路径。

## 降级行为（与设备一致）

- 主机无 FreeType 头文件时，`display_font.c` 的 `__has_include` 保护使其
  编译为无 FreeType 版本，渲染走 FiraSans 内置字体路径。
- sdcard 目录无 `.ttf` 或 `display_font_init` 失败时，传 `NULL` 字体，
  `display_draw_sd_screen` 自行降级（与 `main.c` 行为相同）。

## 错误处理

- CMake 配置时通过 pkg-config 探测 freetype2；找不到则打印警告并继续。
- 运行时 sdcard 目录缺失/无 `.txt`：填充提示文本到 content（仍出图），
  退出码保持 0；PNG 写入失败返回非 0。

## 验证

- `harness/run.sh`：configure + build + run，结束时打印 PNG 路径。
- 烟雾测试：运行后检查 PNG 文件存在且尺寸为 1072x1448。

## 不做的事（YAGNI）

- 不做 CLI 级遮挡参数（`DISPLAY_OCCLUDE_*` 保持编译期宏，与设备一致）。
- 不模拟电子纸波形/灰度残影，输出为理想灰度。
- 不编译 epdiy 源码，不引入 miniz。
