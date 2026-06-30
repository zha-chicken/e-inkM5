# XiaoZhi-Card 小智墨伴

* [小智墨伴说明](https://docs.m5stack.com/zh_CN/core/Xiaozhi_Card_Kit)

## 说明 

小智墨伴由 [M5Stack](https://docs.m5stack.com/) 和 [小智](https://github.com/78/xiaozhi-esp32) 官方合作推出的基于墨水屏的小智语音助手。

本仓库基于 [m5stack/XiaoZhi-Card](https://github.com/m5stack/XiaoZhi-Card)，用于 e-ink M5 / XiaoZhi Card Kit 的定制固件。

## 本版功能

保留原版 XiaoZhi Card Kit 固件能力，包括 4G/ML307 网络、Wi-Fi/4G 切换、墨水屏 UI、触摸设置页、音频输入输出和休眠/关机流程。干净 NVS 下默认网络为 4G/ML307。

新增本地备忘录 MCP：

* 工具名：`self.memo.manage`
* 支持：`list` / `view` / `create` / `update` / `delete` / `clear` / `delete_all`
* 参数：`action`、`id`、`title`、`content`
* 存储：NVS，本地最多 8 条备忘录
* UI：主屏长按约 3 秒打开可滚动备忘录界面，点“关闭”返回

## 固件开发

* [XiaoZhi-Card](./main/boards/xiaozhi-card/README.md)
* [M5Stack XiaoZhi Card Kit 指南](https://docs.m5stack.com/zh_CN/guide/realtime/xiaozhi/xiaozhi_card_kit)

## 构建与刷机

```bash
git clone --recursive https://github.com/zha-chicken/e-inkM5.git
cd e-inkM5
git submodule update --init --recursive
source /path/to/esp-idf/export.sh
idf.py build
idf.py -p /dev/cu.usbmodem3101 erase-flash flash monitor
```

本机已验证环境：

```bash
source ~/.platformio/packages/framework-espidf/export.sh
export PATH="$HOME/.platformio/tools/tool-cmake/bin:$HOME/.platformio/tools/tool-ninja:$PATH"
idf.py build
idf.py -p /dev/cu.usbmodem3101 erase-flash flash
```

验证结果：`xiaozhi.bin` 构建成功，8MB app 分区剩余约 13%；刷入 ESP32-S3-PICO-1 成功；擦除 NVS 后启动日志显示 `Initialize ML307 board`、`Current network type: ML307`，并注册 `self.memo.manage`。
