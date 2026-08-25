<p align="right">
  <strong>简体中文</strong> · <a href="README.en.md">English</a>
</p>

# FoloToy AI Passport · 日历玩法固件

面向 **FoloToy AI Passport**（ESP32-C3 / 8 MB Flash / 无 PSRAM）的**日历玩法固件**，基于官方 [folotoy/ai-passport](https://github.com/folotoy/ai-passport) 项目开发。版本 **0.1.0**。

## 功能简介

一个安静、低功耗的桌面日历：

- **公历月历网格**：6 行 × 7 列，沿用项目的像素风 UI。
- **倒计时**：跟踪目标日期，显示 `D-xxx` / `D+xxx`。
- **纪念日**：标记某个月/日，每年同月同日自动高亮。
- **今天与目标分色**：一目了然。
- **联网自动校时**：接入你自己的 Wi-Fi 后，经 SNTP 拉取真实日期并重置"今天"，不固化单一网络。
- **静音优先**：不启用音频通路，省内存、省电。
- **低功耗**：空闲 30 秒自动熄背光并进入深度低功耗，任意按键重新点亮。

## 直接刷入

可安装文件为 `build/FoloToy-AI-Passport-full.bin`（合并镜像，约 1.5 MB）。

使用官方 **FoloToy Web Tool** 刷机（浏览器内 WebSerial 刷入，地址设为 `0x0`）：

▶ 打开官方刷机工具：<https://tool.folotoy.cn/>

选择固件写入后重启设备，菜单中即出现日历玩法。

```bash
# 或使用命令行刷写（需 ESP-IDF 环境）
idf.py -p PORT flash
```

> 该 `.bin` 也会由自动固件构建工作流（tag 推送）作为 Release 附件发布。

## 官方资源

- 官网 / 产品页：<https://ai-passport.folotoy.cn/>
- 官方刷机工具：<https://tool.folotoy.cn/>
- 官方文档：<https://docs.folotoy.com/>

## 联网与自动校时

通过 `Setup` 玩法选择设备的联网方式，**不与某一个固定网络绑定**：

- **Wi-Fi 自动连接**：连接设备中已保存的凭证（存储在 NVS）。
- **SoftAP 配置页**：设备开启自带热点（`FoloToy-Calendar`，密码 `12345678`）；用手机访问 `http://192.168.4.1` 填入你的网络信息。
- **BLE 配网**：通过一个小型 GATT 服务（`0xFFF0 / 0xFFF1`）接收其他设备下发的 Wi-Fi 凭证与时间戳。

联网成功后，SNTP 拉取真实时间，日历随之重置基准日期并落盘保存——校准结果跨重启延续，且**每个用户都使用自己的 Wi-Fi**。

## 按键操作

| 按键 | 动作 |
| --- | --- |
| 上 / 下 短按 | 上一个月 / 下一个月 |
| Setup 中：上 / 下 | 切换配网方式 |
| Setup 中：OK | 运行当前选中的配网方式 |
| 上 / 下 长按 | 倒计时目标 −1 / +1 天 |
| OK 短按 | 设置倒计时目标 = 今天，并跳转当月 |
| OK 双击 | 设置纪念日 = 今天 |
| OK 长按 | 返回菜单 |

## 构建

需要 ESP-IDF **5.5.3**，目标芯片 `esp32c3`。

```bash
. $IDF_PATH/export.ps1          # 或：source $IDF_PATH/export.sh
idf.py set-target esp32c3
idf.py build
idf.py merge-bin -o build/FoloToy-AI-Passport-full.bin
```

校验：

```bash
./tools/validate.sh --static     # 仓库检查 + 宿主测试（日历逻辑）
./tools/validate.sh --firmware   # ESP-IDF 构建 + 合并镜像校验
```

## 许可

[MIT](./LICENSE) · 变更记录：[docs/CHANGELOG.md](docs/CHANGELOG.md)