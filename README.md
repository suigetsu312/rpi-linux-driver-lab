# Linux Driver 與 Device Model 學習實驗

這個專案主要用來練習寫 Linux driver，並透過 Raspberry Pi 上的實驗認識 Linux device model。
內容涵蓋 Device Tree、platform driver、ILI9341 SPI 顯示與 SPI 觸控。
此儲存庫保留現有實驗程式，不代表各項功能已完成硬體驗證。

## 學習重點

以「硬體如何成為 Linux 裡的裝置，以及驅動如何接手」作為主軸：

1. **描述裝置**：用 Device Tree overlay 描述裝置的 `compatible`、SPI 位址、GPIO 與中斷資源。
2. **認識 bus、device、driver**：platform 與 SPI 是不同匯流排；裝置由對應的匯流排機制管理，
   本專案驅動透過 `of_match_table` 與 Device Tree 的 `compatible` 配對。
3. **理解生命週期**：觀察配對後的 `probe()` 與解除綁定時的 `remove()`，
   練習在 probe 取得資源、初始化硬體，以及處理失敗回傳。
4. **透過核心子系統操作硬體**：使用 GPIO descriptor API、SPI 傳輸 API 與 threaded IRQ，
   觀察 `devm_*` 資源管理和裝置生命週期的關係。
5. **比較不同抽象層**：從自行傳送 ILI9341 指令與 RGB565 資料，延伸到 MIPI DBI 韌體實驗，
   再以觸控 ADC 取樣理解中斷與可睡眠的 SPI 操作如何搭配。

建議閱讀順序是 `drivers/platform/` → `drivers/ili9341/` → `firmware/` → `drivers/touch/`，
並對照各自的 overlay。觸控的 userspace 程式則提供另一種直接觀察 SPI 原始資料的方式。

## 目錄

- `drivers/platform/`：Device Tree 配對與 probe/remove 實驗。
- `drivers/ili9341/`：ILI9341 SPI 驅動、GPIO22 D/C、GPIO23 reset，以及 RGB565 繪圖；程式尺寸為 240×320。
- `drivers/touch/`：SPI ADC X/Y 取樣與 threaded IRQ。
- `overlays/`：platform、SPI0 CS0 LCD、SPI0 CS1 觸控的 overlay 原始碼；觸控中斷使用 GPIO4 下降緣。
- `firmware/ili9341.txt`：MIPI DBI 初始化指令，MADCTL 為 `0xE8`，COLMOD 為 `0x55`。
- `tools/panel-mipi-dbi/`：韌體轉換工具。
- `tests/touch_raw.py`：使用 Python `spidev` 直接讀取 SPI0 CS1 的實驗。

## 建置與檢查

核心模組需要目標 Raspberry Pi 核心對應的標頭與建置環境；WSL 核心標頭不能替代目標環境。
各驅動目錄提供 Makefile，例如在適當的目標建置環境執行：

```sh
make -C drivers/platform KDIR=/path/to/target/kernel/build
make -C drivers/ili9341 KDIR=/path/to/target/kernel/build
make -C drivers/touch KDIR=/path/to/target/kernel/build
```

交叉編譯時另需提供符合目標的 `ARCH` 與 `CROSS_COMPILE`。有安裝 `dtc` 時可檢查 overlay：

```sh
dtc -@ -I dts -O dtb -o /tmp/lab.dtbo overlays/lab-overlay.dts
dtc -@ -I dts -O dtb -o /tmp/ili9341.dtbo overlays/ili9341-overlay.dts
dtc -@ -I dts -O dtb -o /tmp/touch.dtbo overlays/touch-overlay.dts
```

輸出成功仍須在目標系統確認 overlay 套用、驅動配對與實際硬體行為。

## MIPI DBI 韌體

```sh
python3 tools/panel-mipi-dbi/mipi-dbi-cmd /tmp/panel.bin firmware/ili9341.txt
python3 tools/panel-mipi-dbi/mipi-dbi-cmd /tmp/panel.bin
```

工具只依賴 Python 標準函式庫。現有工具 README 指向
<https://github.com/notro/panel-mipi-dbi/wiki>，腳本保留 Noralf Trønnes 的
CC0-1.0 授權與作者標頭；本地副本未記錄上游版本或提交。
保留此工具供文字韌體來源重建二進位；產生的 `firmware/panel.bin` 不納入版本控制。

現有檔案未包含 DRM overlay、顯示時序或開機設定，尚無法單靠此儲存庫重現完整的
320×240 橫向 DRM 部署；也沒有 GPIO27 背光設定或 `config_backup/config.txt`。

## 觸控實驗

`tests/touch_raw.py` 需要目標系統的 Python `spidev` 與可存取的 `/dev/spidev0.1`。
核心觸控實驗使用同一個 SPI 裝置，兩種方式應分別測試；目前核心模組將座標寫入核心日誌，
未提供 Linux input 事件介面。
