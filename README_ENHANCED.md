# ESP32-S3 CD Player (Enhanced)

**基于你提供的项目进行了增强：**
- 保留原有 **CD-DA** 播放（从 USB 笔记本光驱读取音轨，I2S 输出）
- 新增 **蓝牙 A2DP 接收**（手机播放 → ESP32-S3 → I2S DAC 输出）
- 默认屏幕：已保留原项目的 LVGL / TFT 显示
- I2S 输出：沿用原项目 `i2s.c`，可直接外接耳机/功放
- 提供 **GitHub Actions**，提交到 GitHub 后自动编译并产出 `.bin` 固件

> **说明**：MP3 数据光盘播放为后续扩展项。本版首先保证“CD-DA + 蓝牙”稳定可用。蓝牙接入后将自动接管音频输出，CD 播放自动让出 I2S（不与蓝牙同时输出）。

## 如何使用（GitHub 自动编译）
1. 新建一个 GitHub 仓库（Public 或 Private 均可）。
2. 将本压缩包全部内容上传到仓库根目录后推送。
3. 打开仓库的 **Actions** 页面，等待 `ESP32S3 Build` 工作流完成。
4. 构建完成后，进入工作流运行页面，在 **Artifacts** 可下载 `firmware-esp32s3`，其中包含：
   - `bootloader.bin`
   - `partition-table.bin`
   - `CD_Player.bin`（主固件）
   - `flasher_args.json`

## 烧录
使用 `esptool.py`（或 IDF/VSCode）按照 `flasher_args.json` 的地址烧录，或直接用 `idf.py -p PORT flash`。

## 硬件与按键
- USB 光驱接 ESP32-S3 原生 USB OTG（请确保外部 5V 供电充足）
- I2S DAC（PCM5102/ MAX98357A 等）接 `i2s.c` 中定义的 BCK/LRCK/DATA 引脚
- 屏幕接线保持与原工程一致
- 按键定义参考 `components/myDriver/button.h`

## 蓝牙 A2DP
- 上电后自动初始化蓝牙，广播名：`ESP32_CD_Player`
- 手机连接并播放后，蓝牙将接管音频，内部通过环形缓冲 `i2s_fillBuffer()` 推送至 I2S
- 蓝牙断开或停止播放将释放音频占用，CD 播放可继续使用 I2S

## 工程说明
- 本项目采用 **ESP-IDF**（非 Arduino），保留原工程结构
- 在 `main/` 中新增：
  - `bt_a2dp.c/.h`：蓝牙接收与 I2S 对接（数据回调做 18,816 字节累积后再 `i2s_fillBuffer`）
  - 在 `cdPlayer.c` 中对 `i2s_fillBuffer(readCdBuf)` 加了守护：
    ```c
    if (!bt_is_active()) { i2s_fillBuffer(readCdBuf); }
    ```
- 根目录新增 `sdkconfig.defaults` 以启用蓝牙 / USB Host 相关选项
- `.github/workflows/build.yml` 配置了 IDF v5.1.2 云端构建

> 若你的电路使用 I2S 从模式（外部时钟），请按 README 顶部原说明在 `components/myDriver/i2s.c` 中把角色改为 `I2S_ROLE_SLAVE`。

## 后续（可选）
- 集成 ISO9660 解析 + Helix MP3 解码以支持 **数据 CD 的 MP3**；当前主干先保证 CD-DA + 蓝牙稳定。
