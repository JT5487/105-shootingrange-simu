# T91 雙相機系統 - 外場設定標準操作程序 (SOP)

> 本 SOP 設計為問答形式，讓 AI 能夠自動診斷並修復問題。
> 在新環境部署時，請按順序執行以下步驟。

---

## 🚀 快速設置流程

### 步驟 1：硬體確認

**AI 診斷問題：**
```
Q1: 請確認以下硬體連接是否完成？
- [ ] 兩台 Basler GigE 相機已透過網路線連接到 Switch
- [ ] Switch 已連接到電腦的 GigE 網路埠
- [ ] 兩台投影機已連接並顯示桌面延伸畫面
- [ ] 雷射發射器電池充足
```

**AI 自動測試命令：**
```powershell
# 檢查網路設備
Get-NetAdapter | Where-Object {$_.Status -eq 'Up'}
```

---

### 步驟 2：相機偵測

**AI 診斷問題：**
```
Q2: PylonViewer 能否看到兩台相機？
- 開啟 PylonViewer 64-Bit
- 勾選 "Show Cameras Only"
- 確認看到：Basler acA640-90gm (23058324) 和 (23058325)
```

**如果只看到一台相機：**
1. 檢查網路線連接
2. 開啟 Pylon IP Configurator 設定正確的 IP

**AI 自動修復：**
- 確保 PylonViewer 已關閉後再啟動後端

---

### 步驟 3：相機參數設定 (PylonViewer)

**AI 診斷問題：**
```
Q3: 每台相機是否能在 PylonViewer 中看到雷射點？

對每台相機執行：
1. 選擇相機並按 F5 開始連續擷取
2. Pixel Format = Mono 8（必須！）
3. Exposure Auto = Off
4. Gain Auto = Off
5. ExposureTime = 8000
6. GainRaw = 808（或更高）
7. 用雷射對著相機，確認預覽中能看到亮點
```

**常見問題修復：**
| 症狀 | 解決方案 |
|------|----------|
| Gain 只能調到 400 | Pixel Format 改為 Mono 8 |
| 看不到雷射 | 提高 GainRaw 到 2000-4000 |
| 畫面全白 | 降低 ExposureTime 或 Gain |

---

### 步驟 4：編譯後端（如有需要）

**AI 診斷問題：**
```
Q4: 程式碼是否需要重新編譯？
- 如果修改過 .cpp 或 .hpp 檔案，需要重新編譯
```

**AI 自動命令：**
```powershell
cd "c:\Users\CMA\Desktop\105軟體 (C++)-20251222T161257Z-1-001\105軟體 (C++)\cpp"
cmd /c "set PATH=C:\msys64\mingw64\bin;%PATH% && mingw32-make -f Makefile.win clean && mingw32-make -f Makefile.win"
```

---

### 步驟 5：啟動後端

**AI 診斷問題：**
```
Q5: PylonViewer 是否已關閉？（必須關閉才能讓程式使用相機）
```

**AI 自動命令：**
```powershell
cd "c:\Users\CMA\Desktop\105軟體 (C++)-20251222T161257Z-1-001\105軟體 (C++)\cpp"
cmd /c "set PATH=C:\msys64\mingw64\bin;%PATH% && t91_tracker.exe"
```

**預期成功輸出：**
```
[INFO] Found 2 Basler device(s) after initial scan.
[INFO] Camera A initialized.
[INFO] Camera B initialized.
```

**常見錯誤及修復：**

| 錯誤訊息 | 原因 | 修復 |
|----------|------|------|
| `Pylon found 0 devices` | PylonViewer 佔用相機 | 關閉 PylonViewer |
| `Camera A failed to open` | 相機未偵測到 | 檢查網路連接 |
| `PylonDeviceOpen failed` | 相機被佔用 | 關閉其他使用相機的程式 |

---

### 步驟 6：設定閾值

**AI 自動命令：**
```powershell
Invoke-RestMethod -Uri "http://localhost:8081/command" -Method Post -ContentType "application/json" -Body '{"action":"update_threshold","data":{"threshold":80}}'
```

---

### 步驟 7：開啟測試頁面

**AI 診斷問題：**
```
Q6: 將測試頁面拖到對應的投影幕
- calibration_grid_a.html → 左投影幕（藍色邊框）
- calibration_grid_b.html → 右投影幕（橘色邊框）
```

---

### 步驟 8：執行校正

**AI 診斷問題：**
```
Q7: 是否需要重新校正？
- 如果相機位置改變，需要重新校正
- 如果 calibration_a.json 或 calibration_b.json 不存在，需要校正
```

**AI 自動命令（啟動校正模式）：**
```powershell
Invoke-RestMethod -Uri "http://localhost:8081/command" -Method Post -ContentType "application/json" -Body '{"action":"start_calibration"}'
```

**校正流程：**
1. 對左螢幕依序射擊 4 個角（左上→右上→右下→左下）
2. 對右螢幕依序射擊 4 個角

**AI 驗證狀態：**
```powershell
Invoke-RestMethod -Uri "http://localhost:8081/state" -Method Get | Select-Object calib_count_a, calib_count_b, is_calibrating
```

**預期結果：**
```
calib_count_a: 4
calib_count_b: 4
is_calibrating: False
```

---

### 步驟 9：功能測試

**AI 診斷問題：**
```
Q8: 紅點是否正確顯示？
- 射擊左螢幕 → 只有左螢幕出現紅點
- 射擊右螢幕 → 只有右螢幕出現紅點
```

**如果兩個螢幕都出現紅點：** 重新啟動後端

---

## 🛠️ 緊急故障排除

### 問題 A: 程式完全無法啟動

```powershell
# 1. 確認相關程序都已關閉
Get-Process | Where-Object {$_.ProcessName -like "*pylon*" -or $_.ProcessName -like "*t91*"} | Stop-Process -Force

# 2. 等待 3 秒
Start-Sleep -Seconds 3

# 3. 重新啟動
cd "c:\Users\CMA\Desktop\105軟體 (C++)-20251222T161257Z-1-001\105軟體 (C++)\cpp"
cmd /c "set PATH=C:\msys64\mingw64\bin;%PATH% && t91_tracker.exe"
```

### 問題 B: 校正後紅點位置不準

1. 刪除校正檔案：
```powershell
Remove-Item "c:\Users\CMA\Desktop\105軟體 (C++)-20251222T161257Z-1-001\105軟體 (C++)\cpp\calibration_a.json" -Force
Remove-Item "c:\Users\CMA\Desktop\105軟體 (C++)-20251222T161257Z-1-001\105軟體 (C++)\cpp\calibration_b.json" -Force
```

2. 重新啟動後端
3. 重新執行校正

### 問題 C: 相機偵測不到雷射

1. 在 PylonViewer 中調整參數（見步驟 3）
2. 確認 Pixel Format = Mono 8
3. 提高 GainRaw 到 2000-8000
4. 檢查濾光片類型（應為 IR Pass）

---

## 📋 部署檢查清單

```
□ PylonViewer 確認兩台相機都能看到
□ 兩台相機都能在 PylonViewer 中看到雷射點
□ PylonViewer 已關閉
□ 後端啟動顯示 "Found 2 Basler device(s)"
□ 閾值設為 80
□ 左螢幕顯示 calibration_grid_a.html（藍色邊框）
□ 右螢幕顯示 calibration_grid_b.html（橘色邊框）
□ 左右螢幕各完成 4 點校正
□ 射擊測試：紅點只出現在對應螢幕
□ 完成！
```
