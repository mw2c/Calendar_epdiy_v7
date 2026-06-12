# Render Harness

在电脑上直接编译运行设备端渲染代码（`main/display_screen.c` 等，零修改），
输出渲染完成的 PNG，无需烧录开发板。

## 依赖

- CMake >= 3.16，主机 C 编译器（Xcode CLT 自带 clang 即可）
- zlib（macOS 系统自带）
- FreeType（可选）：`brew install freetype`；缺失时自动降级为内置 FiraSans 字体

## 用法

```sh
./harness/run.sh
open harness/out/render.png
```

可选参数：

```sh
./harness/run.sh --sdcard <目录> --out <png路径>
```

注意：默认路径相对仓库根目录，run.sh 已自动切换；直接运行
`harness/build/render_harness` 时请在仓库根目录执行。

## 模拟 SD 卡

`harness/assets/sdcard/` 模拟设备的 SD 卡：

- 第一个（按文件名排序）`.txt` 作为显示内容，截断行为与设备一致
  （`SD_MAX_TXT_BYTES` = 2300 字节）
- 字体优先用 `app_config.h` 中 `SD_DEFAULT_FONT_PATH` 配置的路径，
  否则取 `fonts/` 下按文件名排序第一个 `.ttf`/`.otf`
  （字体文件不入库，自行放入）

## 工作原理

设备和 harness 编译**同一套源文件**，没有渲染逻辑的拷贝：

- 应用渲染源码（`main/display_screen.c` 等）原样编译，include 真实的
  `components/epdiy2/src/epdiy.h`
- epdiy 渲染源码也原样编译：`epdiy.c`（旋转、像素/矩形）、`font.c`
  （glyph 解压与文字绘制）、`displays.c`、`builtin_waveforms.c`
- `src/epd_stub.c` 只 stub 硬件边界：板级/渲染层函数 no-op
  （`epd_renderer_init`、`epd_draw_base`、`epd_clear_area` 等），
  并实现 `epd_hl_*`——`epd_hl_update_screen()` 在主机上的语义是把
  framebuffer 按 `EPD_ROT_PORTRAIT` 方向导出为 1072x1448 灰度 PNG
- `src/esp_shim/` 提供假 ESP-IDF 头文件（日志转 stderr、heap_caps 转
  malloc）；`miniz.h` + `src/miniz_shim.c` 把 font.c 用的 tinfl 解压接口
  转到系统 zlib

## 测试

```sh
ctest --test-dir harness/build --output-on-failure
```

包含 stub 行为单元测试（旋转映射、nibble 布局、glyph 解压绘制、PNG 头）
和整机烟雾测试（render_smoke）。

## 局限

- 不模拟电子纸波形、残影、刷新闪烁，输出为理想灰度
- `epd_clear()` 在主机上是 no-op（设备上是面板闪刷，不影响 framebuffer）
- `DISPLAY_OCCLUDE_*` 等仍是编译期宏，与设备一致；改动后需重新构建
