# T91 紅外線射擊追蹤系統 - 已知風險清單

**版本:** 1.0  
**日期:** 2026-02-05  
**狀態:** 初始版本  
**依據:** AI-Assisted Development Constitution v2.0 Article 9

---

## 文件目的

本文件記錄 T91 系統的**所有已知風險**、**邊界情況**、**脆弱點**以及**潛在故障模式**。

根據憲章 Article 9：

> "After generating any significant output, you MUST verify the human can answer:
> - **Why** was it designed this way?
> - **Where** are the risks?
> - **What** is most likely to break?"

**目標讀者:**
- 系統維護人員
- 新開發者
- 部署工程師
- 射擊訓練教官

**風險等級定義:**
- 🔴 **CRITICAL** - 可能導致系統完全失效或安全問題
- 🟠 **HIGH** - 影響核心功能，需要人工介入恢復
- 🟡 **MEDIUM** - 影響部分功能或使用者體驗
- 🟢 **LOW** - 輕微影響，有 workaround

---

## 風險總覽表

| ID | 風險名稱 | 類別 | 等級 | 偵測方式 | 緩解措施 |
|----|---------|------|------|---------|---------|
| RISK-001 | GigE 網路頻寬飽和 | 硬體 | 🟠 HIGH | 封包遺失計數 | 限制 45fps |
| RISK-002 | Homography 邊緣扭曲 | 演算法 | 🟠 HIGH | 校準驗證射擊 | 校準點離邊緣 5% |
| RISK-003 | 校準模式中誤觸發射擊 | 狀態管理 | 🟡 MEDIUM | 檢查 calibration_mode_ | 互斥邏輯 |
| RISK-004 | 檔案寫入失敗 | 檔案系統 | 🟡 MEDIUM | 例外處理 | 日誌錯誤，繼續運行 |
| RISK-005 | 相機單點故障 | 硬體 | 🟠 HIGH | open() 失敗 | 繼續執行（部分功能）|
| RISK-006 | 重複射擊誤判 | 演算法 | 🟡 MEDIUM | 時間戳與距離檢查 | 200ms + 0.05 閾值 |
| RISK-007 | 未認證的遠端存取 | 安全性 | 🔴 CRITICAL | **無** | **缺乏** (CRITICAL-007) |
| RISK-008 | 惡意輸入導致崩潰 | 安全性 | 🔴 CRITICAL | **無** | **缺乏** (HIGH-013) |
| RISK-009 | Chrome 強制關閉遺留程序 | 生命週期 | 🟡 MEDIUM | Process monitor | Graceful shutdown |
| RISK-010 | Homography 矩陣奇異 | 演算法 | 🟡 MEDIUM | 檢查行列式 | 重新校準 |
| RISK-011 | 執行緒競爭條件 | 並發 | 🟡 MEDIUM | Thread Sanitizer | Mutex 保護 |
| RISK-012 | 射手區域邊界模糊 | 演算法 | 🟢 LOW | X 座標 = -0.67/0/... | 屬於右側射手 |
| RISK-013 | IR 環境光干擾 | 環境 | 🟡 MEDIUM | Threshold 過低 | 調整 threshold |
| RISK-014 | 記憶體洩漏 | 資源管理 | 🟢 LOW | Valgrind | RAII + smart pointers |
| RISK-015 | 相機對調導致射手錯誤 | 部署 | 🟠 HIGH | 序號檢查 | 標籤相機 |

---

## 第一部分：硬體風險

## RISK-001: GigE 網路頻寬飽和 🟠 HIGH

### 風險描述

兩個 Basler 相機共享同一個 GigE 網路埠，若幀率過高或網路壅塞，可能導致封包遺失。

### 技術細節

**頻寬計算:**
- 單相機: 1920×1200 × 1 byte/pixel × 45 fps ≈ 103 MB/s
- 雙相機: 103 × 2 ≈ 206 MB/s
- GigE 理論上限: 1 Gbps = 125 MB/s

**實際問題:**
- 206 MB/s > 125 MB/s → **超過理論上限**
- 實際上 GigE 有協定開銷，可用頻寬約 110 MB/s
- 當前設定 45 fps 勉強可行，若增加會立即飽和

### 發生條件

- 幀率 > 60 fps
- 網路埠與其他裝置共享（如 NAS、印表機）
- 網路線品質不佳（Cat5 而非 Cat6）
- 網路卡驅動過舊

### 症狀

- Console 顯示 `[DEBUG] Grab S/N ... error`
- 影像幀跳格（時間戳不連續）
- 偵測延遲增加
- 射擊記錄遺失

### 偵測方式

**程式碼位置:** `CameraManager.cpp:156`

```cpp
if (!camera_grab_successful) {
    std::cerr << "[DEBUG] Grab S/N ... error" << std::endl;
}
```

**建議監控:**
- 計數 grab 錯誤次數
- 每 100 幀統計一次成功率
- 若成功率 < 95%，記錄警告

### 緩解措施

**當前:**
- ✅ 限制幀率為 45 fps (CameraManager.cpp:115)

**建議改進:**
1. **專用網路埠**
   - 相機連接到專用 GigE 網路卡
   - 不與其他裝置共享

2. **Jumbo Frames**
   - 設定 MTU = 9000 (預設 1500)
   - 降低協定開銷
   - 需要交換器支援

3. **網路品質**
   - 使用 Cat6 或更好的網路線
   - 確保線路長度 < 100m
   - 避免電磁干擾源

4. **動態調整**
   - 監控封包遺失率
   - 自動降低幀率（60 → 45 → 30）

### 相關不變量

- INV-HW-002: 相機幀率固定 45fps
- INV-HW-003: GigE 網路專用

---

## RISK-002: Homography 邊緣扭曲 🟠 HIGH

### 風險描述

4 點 homography 在螢幕邊緣附近的轉換精度降低，可能導致命中點偏移 5-10 公分。

### 技術細節

**數學原理:**
- Homography 是**透視變換**，假設平面映射
- 使用 4 個角點計算，內插中間點
- 離角點越遠，累積誤差越大
- 邊緣區域受鏡頭畸變影響

**誤差分佈:**
```
螢幕位置         典型誤差
中心 (0, 0)      < 1 cm
中等 (±0.5)      2-3 cm
邊緣 (±0.9)      5-10 cm
角落 (±1.0)      10-15 cm
```

### 發生條件

- 校準點過於接近螢幕邊緣（距離 < 5%）
- 相機鏡頭畸變未校正
- 螢幕與相機角度過大（> 30°）
- 射手站位過於偏左/右

### 症狀

- 邊緣射擊一致性偏移（例如左上角總是偏高）
- 中心準確但角落不準
- 不同距離模式下誤差放大

### 偵測方式

**校準驗證程序:**
1. 完成 4 點校準
2. 射擊已知位置（例如螢幕中心、四個角）
3. 比較預期座標與偵測座標
4. 計算誤差 (RMS error)

**建議閾值:**
- 中心誤差 < 2 cm → 優秀
- 邊緣誤差 < 5 cm → 可接受
- 邊緣誤差 > 10 cm → 需重新校準

### 緩解措施

**當前:**
- ✅ 校準點設定於 `dst_points_` = (±0.85, ±0.85)
- 距離邊緣約 7.5%，避開最差區域

**建議改進:**
1. **使用更多校準點**
   - 從 4 點增加到 9 點（3×3 網格）
   - 使用 `cv::findHomography()` RANSAC 模式
   - 提升邊緣精度

2. **鏡頭畸變校正**
   - 預先校正相機鏡頭畸變
   - 使用 `cv::undistort()`
   - 需要額外的相機內參校準

3. **限制有效區域**
   - 標示「不建議射擊區域」（螢幕外圍 5%）
   - UI 顯示綠色安全區、紅色警告區
   - 邊緣射擊結果標記 `[LOW_CONFIDENCE]`

4. **動態誤差修正**
   - 建立誤差地圖（2D 插值）
   - 根據位置應用補償
   - 需要大量校準資料

### 相關不變量

- INV-CALIB-001: 校準點對應順序
- INV-CALIB-003: Homography 矩陣 3×3
- INV-DATA-003: 座標正規化範圍 [-1, 1]

---

## RISK-003: 校準模式中誤觸發射擊 🟡 MEDIUM

### 風險描述

在校準模式下，使用者點擊螢幕角落時可能被誤判為射擊，污染訓練記錄。

### 技術細節

**當前邏輯 (IRTracker.cpp:461-672):**
```cpp
if (calibration_mode_ == "a") {
    // 收集校準點
} else {
    // 正常追蹤，執行命中判定
}
```

**問題:**
- 模式切換可能有延遲
- UI 點擊與雷射射擊使用相同 IR 點偵測
- 無明確的「校準完成」確認

### 發生條件

- 快速切換校準模式與追蹤模式
- 校準點擊時手部晃動導致多個 IR 點
- 前端 UI 與後端狀態不同步

### 症狀

- 射擊記錄中出現不合理座標（接近 ±0.85 角落）
- 射擊時間與訓練開始時間重疊
- CSV 檔案中出現 "校準" 相關註記（若有）

### 偵測方式

**程式碼檢查:**
- 檢查 `calibration_mode_` 在 processingLoop 中的狀態
- 確認互斥邏輯正確

**日誌監控:**
- 記錄校準模式切換時間
- 記錄射擊時間
- 若時間差 < 1 秒，標記為可疑

### 緩解措施

**當前:**
- ✅ 使用 if-else 互斥邏輯
- ✅ `calibration_mode_` 受 mutex 保護

**建議改進:**
1. **明確的冷卻期**
   ```cpp
   if (mode_switch_time_.elapsed() < 2000ms) {
       return;  // 忽略所有偵測
   }
   ```

2. **UI 視覺回饋**
   - 校準模式下顯示明顯邊框（紅色閃爍）
   - 顯示文字："校準中 - 請勿射擊"
   - 播放音效提示

3. **射擊過濾**
   - 校準模式下的偵測不記錄到 latest_shots_
   - 完成校準後清空緩衝區
   - 防止殘留資料污染

4. **確認機制**
   - 校準完成後要求使用者確認
   - 顯示校準點位置供檢查
   - 「取消」按鈕重新校準

### 相關不變量

- INV-STATE-002: 校準模式與追蹤互斥
- INV-CALIB-004: 校準模式互斥

---

## 第二部分：演算法風險

## RISK-006: 重複射擊誤判 🟡 MEDIUM

### 風險描述

同一射擊可能被偵測多次（相機抓取連續幀），導致命中計數虛高。

### 技術細節

**問題來源:**
- 相機 45 fps，每幀間隔 ~22ms
- 雷射點可能持續 50-100ms
- 可能在 2-4 幀中偵測到同一射擊

**當前過濾邏輯 (IRTracker.cpp:581-616):**
```cpp
// 時間過濾
if (time_diff < 200ms && distance_sq < 0.05) {
    continue;  // 視為重複
}

// 最小間隔
if (time_diff < 300ms) {
    continue;  // 射擊過快
}
```

**參數:**
- 200ms - 重複偵測窗口（魔術數字 HIGH-006）
- 0.05 - 距離閾值（正規化座標平方）
- 300ms - 最小射擊間隔

### 發生條件

- 雷射筆停留時間過長
- 射擊速度極快（< 300ms）
- 相機幀率極高（若調整為 60fps+）
- 座標抖動（手部不穩）

### 症狀

- 單次射擊被計數 2-3 次
- latest_shots_ 中有時間戳非常接近的項目
- 命中計數與實際射擊次數不符

### 偵測方式

**後處理分析:**
```python
# 分析 latest_shots_
for i in range(1, len(shots)):
    time_diff = shots[i].timestamp - shots[i-1].timestamp
    dist = distance(shots[i].position, shots[i-1].position)
    if time_diff < 300 and dist < 0.1:
        print(f"可疑重複: {shots[i]}")
```

**即時監控:**
- 記錄過濾掉的重複次數
- 若重複率 > 20%，調整閾值

### 緩解措施

**當前:**
- ✅ 雙重過濾：時間 + 距離
- ✅ 最小射擊間隔 300ms

**建議改進:**
1. **自適應閾值**
   - 根據射手速度動態調整
   - 快速射手 → 降低閾值（250ms）
   - 慢速射手 → 提升閾值（500ms）

2. **狀態機過濾**
   ```
   狀態: IDLE → DETECTING → COOLDOWN → IDLE
   DETECTING: 持續偵測 IR 點，記錄峰值
   COOLDOWN: 200ms 內忽略所有偵測
   ```

3. **強度變化偵測**
   - 記錄 IR 點強度
   - 若強度先增後減（拋物線），視為單次射擊
   - 多次射擊會有多個峰值

4. **使用者回報機制**
   - UI 顯示最近 5 次射擊
   - 允許使用者標記誤判
   - 收集資料優化演算法

### 相關不變量

- INV-DATA-005: 射擊記錄緩衝區上限 50
- INV-STATE-003: 射擊時間戳遞增

---

## RISK-010: Homography 矩陣奇異 🟡 MEDIUM

### 風險描述

若 4 個校準點共線或接近共線，`cv::findHomography()` 可能回傳奇異矩陣，導致座標轉換錯誤。

### 技術細節

**奇異條件:**
- 4 點共線（行列式 = 0）
- 3 點共線，1 點略偏（行列式接近 0）
- 點的順序錯誤（例如 TL → BL → TR → BR）

**數學:**
```
Homography H 是 3×3 矩陣
det(H) ≈ 0 → 奇異矩陣
→ H⁻¹ 不存在
→ 座標轉換失敗或極度不穩定
```

### 發生條件

- 使用者未依照 TL → TR → BR → BL 順序點擊
- 點擊位置過於接近一條線
- 手部抖動導致點位置偏移
- 螢幕顯示區域扭曲（非矩形）

### 症狀

- 轉換後座標極度扭曲（例如 x > 100）
- 所有射擊偵測為同一射手
- 命中判定完全失效
- Console 顯示 OpenCV 警告

### 偵測方式

**檢查行列式:**
```cpp
double det = cv::determinant(homography_a_);
if (std::abs(det) < 1e-6) {
    std::cerr << "Warning: Homography is singular!" << std::endl;
    return false;
}
```

**檢查條件數:**
```cpp
cv::Mat eigenvalues;
cv::eigen(homography_a_, eigenvalues);
double condition_number = eigenvalues.at<double>(0) / eigenvalues.at<double>(8);
if (condition_number > 1e6) {
    std::cerr << "Warning: Homography is ill-conditioned!" << std::endl;
}
```

### 緩解措施

**當前:**
- ⚠️ **無驗證** - 直接使用 `cv::findHomography()` 結果

**建議改進:**
1. **即時驗證**
   ```cpp
   cv::Mat H = cv::findHomography(src_points, dst_points);
   if (std::abs(cv::determinant(H)) < 1e-6) {
       throw std::runtime_error("Calibration failed: points are collinear");
   }
   ```

2. **UI 視覺回饋**
   - 顯示 4 個點的連線
   - 檢查是否形成凸四邊形
   - 若接近一條線，顯示警告

3. **自動修正**
   - 偵測點順序錯誤
   - 自動重新排序（基於幾何關係）
   - 提示使用者重新點擊

4. **備用方案**
   - 若當前校準失敗，回退到上一次有效校準
   - 或提示使用預設校準（若存在）

### 相關不變量

- INV-CALIB-001: 校準點對應順序
- INV-CALIB-003: Homography 矩陣 3×3

---

## RISK-012: 射手區域邊界模糊 🟢 LOW

### 風險描述

射手區域邊界（X = -0.67, -0.33, 0, 0.33, 0.67）的射擊歸屬不明確。

### 技術細節

**當前規則 (IRTracker.cpp:536-541):**
```cpp
int shooter_id = static_cast<int>((pt.x + 1.0) / (2.0 / 3.0)) + 1;
```

**邊界情況:**
```
X = -0.67 → (1.33 / 0.67) = 1.98 → shooter_id = 2 (不是 1)
X = 0.00 → (1.00 / 0.67) = 1.49 → shooter_id = 2 (不是 3)
X = 0.67 → (1.67 / 0.67) = 2.49 → shooter_id = 3 (不是 4)
```

**規則:** 邊界值屬於**右側射手**（因為 static_cast 截斷）

### 發生條件

- 射手站位恰好在兩個區域交界
- 射擊非常接近邊界（誤差 < 1cm）
- 歸零偏移將射擊推到邊界

### 症狀

- 射手 2 的命中被計入射手 3
- 邊界附近一致性偏差
- 使用者困惑「我明明打中了」

### 偵測方式

**資料分析:**
- 統計每個射手的命中分佈
- 檢查是否有「邊界效應」（X 接近 ±0.67 時計數異常）

**視覺化:**
- 繪製所有射擊點
- 標註區域邊界
- 檢查邊界附近的歸屬

### 緩解措施

**當前:**
- ✅ 邊界規則一致（右側射手）
- ✅ 有上下界限制 (shooter_id clamping)

**建議改進:**
1. **容錯區域**
   ```cpp
   // 邊界附近 ±2% 視為「模糊區域」
   if (std::abs(pt.x - boundary) < 0.02) {
       // 根據最近射擊記錄判斷
       // 或詢問使用者
   }
   ```

2. **視覺邊界標示**
   - UI 顯示 6 條垂直線（區域分隔）
   - 邊界區域標為黃色警告帶
   - 建議射手避開邊界

3. **歸零偏移檢查**
   - 若歸零偏移將命中推到邊界，記錄警告
   - 建議使用者調整歸零值

4. **記錄邊界資訊**
   - 在 CSV 中標記 `[BOUNDARY]`
   - 供事後分析使用

### 相關不變量

- INV-COORD-001: 相機到射手映射
- INV-COORD-003: 射手區域等寬劃分

---

## 第三部分：安全性風險

## RISK-007: 未認證的遠端存取 🔴 CRITICAL

### 風險描述

HTTP API 完全無認證機制，任何網路用戶端可執行所有操作，包括系統關機。

### 技術細節

**當前狀態:**
- ✅ HTTP 伺服器監聽 `0.0.0.0:8081`（所有網路介面）
- ❌ **無** `Authorization` header 檢查
- ❌ **無** HMAC token 驗證
- ❌ **無** IP 白名單
- ❌ **無** 速率限制

**攻擊向量:**
1. **遠端關機**
   ```bash
   curl -X POST http://<target>:8081/command \
     -d '{"action":"shutdown_system"}'
   ```

2. **竄改配置**
   ```bash
   curl -X POST http://<target>:8081/command \
     -d '{"action":"adjust_targets","data":{"scale":9999}}'
   ```

3. **觸發校準**
   ```bash
   curl -X POST http://<target>:8081/command \
     -d '{"action":"start_calibration_a"}'
   # 系統進入校準模式，無法追蹤射擊
   ```

### 發生條件

- 系統連接到公共網路
- 與其他不受信任裝置共享區域網路
- 攻擊者掃描網路發現 8081 埠
- 釣魚攻擊誘騙內部人員執行惡意請求

### 症狀

- 系統突然關機
- 配置參數異常
- 射擊記錄遺失或錯誤
- UI 顯示異常狀態

### 偵測方式

**日誌監控:**
- 記錄所有 HTTP 請求的來源 IP
- 檢查是否有非預期 IP
- 統計敏感操作（shutdown, calibration）頻率

**網路監控:**
- 使用防火牆規則限制來源
- 監控異常流量模式

### 緩解措施

**當前:**
- ❌ **完全缺乏** (CRITICAL-007 違規)

**必須立即實施:**
1. **HMAC Token 認證**
   ```cpp
   const std::string SECRET_TOKEN = loadFromFile("config/auth_token.txt");
   
   svr.Post("/command", [&](const httplib::Request &req, httplib::Response &res) {
       std::string client_token = req.get_header_value("X-Auth-Token");
       if (!constant_time_compare(client_token, SECRET_TOKEN)) {
           res.status = 403;
           res.set_content(json({{"error", "Unauthorized"}}).dump(), "application/json");
           return;
       }
       // ... 正常處理
   });
   ```

2. **IP 白名單**
   ```cpp
   const std::set<std::string> ALLOWED_IPS = {"127.0.0.1", "192.168.1.100"};
   
   std::string client_ip = req.remote_addr;
   if (ALLOWED_IPS.find(client_ip) == ALLOWED_IPS.end()) {
       res.status = 403;
       return;
   }
   ```

3. **速率限制**
   ```cpp
   std::map<std::string, RateLimiter> rate_limiters;
   
   if (!rate_limiters[client_ip].allow_request()) {
       res.status = 429;  // Too Many Requests
       return;
   }
   ```

4. **分級權限**
   - 唯讀操作（/shots, /state）- 無需認證或低權限 token
   - 配置修改 - 需要標準 token
   - 系統控制（shutdown, calibration）- 需要管理員 token

### 相關不變量

- INV-SEC-001: HTTP 端點需要認證（當前違反）

---

## RISK-008: 惡意輸入導致崩潰 🔴 CRITICAL

### 風險描述

HTTP API 缺乏輸入驗證，惡意或錯誤的 JSON 可導致系統崩潰。

### 技術細節

**當前漏洞 (main.cpp):**
```cpp
// Line 249-250: 無 try-catch
double zx = data["zeroingX"].get<double>();  // 若型別錯誤會拋例外

// Line 206-217: 無範圍檢查
allTargetSettings[key]["scale"] = data["scale"];  // scale 可能是負數或 1e308
```

**攻擊向量:**
1. **型別錯誤**
   ```json
   {"action": "adjust_global_zeroing", "data": {"zeroingX": "NOT_A_NUMBER"}}
   → 拋出 nlohmann::json::type_error
   → 未捕捉 → 程式崩潰
   ```

2. **範圍溢位**
   ```json
   {"action": "adjust_targets", "data": {"scale": 999999}}
   → 目標尺寸爆炸
   → 所有射擊誤判
   ```

3. **陣列越界**
   ```json
   {"action": "adjust_shooter_zeroing", "data": {"shooterId": 999}}
   → 陣列越界存取
   → Segmentation fault
   ```

### 發生條件

- 攻擊者發送惡意請求
- 前端 UI bug 發送錯誤 JSON
- 網路傳輸錯誤導致 JSON 損壞
- 自動化工具格式錯誤

### 症狀

- 系統突然崩潰
- 重啟後配置損壞
- 射擊追蹤異常
- Console 顯示例外訊息

### 偵測方式

**例外監控:**
```cpp
try {
    auto j = json::parse(req.body);
    // ... 處理
} catch (const json::parse_error& e) {
    std::cerr << "JSON parse error: " << e.what() << std::endl;
} catch (const json::type_error& e) {
    std::cerr << "JSON type error: " << e.what() << std::endl;
} catch (const std::out_of_range& e) {
    std::cerr << "Out of range: " << e.what() << std::endl;
}
```

**日誌記錄:**
- 記錄所有解析失敗的請求
- 記錄來源 IP 和時間
- 自動封鎖頻繁錯誤的 IP

### 緩解措施

**當前:**
- ⚠️ 外層有 try-catch (main.cpp:142)，但內部直接 `.get<T>()` 無保護

**必須立即實施:**
1. **輸入驗證函數**
   ```cpp
   double safe_get_double(const json& j, const char* key, 
                          double min, double max, double default_val) {
       try {
           if (!j.contains(key)) return default_val;
           double val = j[key].get<double>();
           if (val < min || val > max) {
               throw std::out_of_range("Value out of range");
           }
           return val;
       } catch (const json::type_error&) {
           return default_val;
       }
   }
   ```

2. **範圍檢查表**
   | 參數 | 最小值 | 最大值 | 預設值 |
   |------|--------|--------|--------|
   | shooter_id | 1 | 6 | - |
   | zeroingX/Y | -100.0 | 100.0 | 0.0 |
   | scale | 0.1 | 10.0 | 1.0 |
   | vertical | -1.0 | 1.0 | 0.0 |
   | threshold | 0 | 255 | 200 |

3. **錯誤回應**
   ```cpp
   res.status = 400;  // Bad Request
   res.set_content(json({
       {"error", "Invalid input"},
       {"field", "zeroingX"},
       {"expected", "double in [-100, 100]"},
       {"received", data["zeroingX"]}
   }).dump(), "application/json");
   ```

4. **Schema 驗證**
   - 使用 JSON Schema 驗證所有輸入
   - 拒絕不符合 schema 的請求
   - 自動產生錯誤訊息

### 相關不變量

- INV-SEC-002: 輸入必須經過驗證（當前違反）
- INV-DATA-001: 射手 ID 範圍 [1, 6]

---

## 第四部分：硬體風險

## RISK-005: 相機單點故障 🟠 HIGH

### 風險描述

若單一相機故障，系統可部分運行但 3 個射手無法追蹤。

### 技術細節

**當前行為 (IRTracker.cpp:22-46):**
```cpp
bool cam_a_opened = cam_a->open();
bool cam_b_opened = cam_b->open();

if (!cam_a_opened) {
    std::cerr << "無法開啟相機 A" << std::endl;
}
if (!cam_b_opened) {
    std::cerr << "無法開啟相機 B" << std::endl;
}

// 繼續執行，即使相機失敗 (IRTracker.cpp:130-135)
```

**影響範圍:**
| 故障相機 | 受影響射手 | 可用射手 |
|---------|-----------|---------|
| Camera A | 1, 2, 3 | 4, 5, 6 |
| Camera B | 4, 5, 6 | 1, 2, 3 |
| 兩者 | 全部 | 無 |

### 發生條件

- 相機硬體故障
- GigE 網路線鬆脫
- 電源不足（PoE 功率不夠）
- 驅動程式衝突
- Pylon SDK 初始化失敗

### 症狀

- Console 顯示 "無法開啟相機 X"
- 特定射手（1-3 或 4-6）完全無回應
- 其他射手正常運作
- UI 可能無明顯錯誤提示

### 偵測方式

**啟動檢查:**
```cpp
int cameras_ok = (cam_a_opened ? 1 : 0) + (cam_b_opened ? 1 : 0);
if (cameras_ok == 0) {
    throw std::runtime_error("No cameras available");
} else if (cameras_ok == 1) {
    std::cerr << "[WARNING] Only one camera available, partial functionality" << std::endl;
}
```

**執行時監控:**
- 每 1000 幀檢查 grab 成功率
- 若成功率 < 10%，標記相機為故障
- 嘗試重新連接（每 30 秒）

### 緩解措施

**當前:**
- ⚠️ 寬鬆檢查，允許部分功能（HIGH-001 相關）

**建議改進:**
1. **嚴格模式選項**
   ```cpp
   if (config.require_both_cameras && cameras_ok < 2) {
       throw std::runtime_error("Both cameras required");
   }
   ```

2. **UI 狀態顯示**
   - 顯示兩個相機的即時狀態（綠色/紅色）
   - 標示哪些射手受影響
   - 提供重新連接按鈕

3. **自動重連機制**
   ```cpp
   if (!cam_a_opened) {
       std::thread([this]() {
           while (running_) {
               std::this_thread::sleep_for(30s);
               if (cam_a->open()) {
                   std::cerr << "Camera A reconnected" << std::endl;
                   break;
               }
           }
       }).detach();
   }
   ```

4. **降級模式**
   - 若 Camera A 失敗，將射手 1-3 的資料標記為 `[UNAVAILABLE]`
   - 自動調整 UI 佈局，只顯示可用射手
   - 記錄警告到日誌

5. **備用相機**
   - 支援熱插拔備用相機
   - 自動偵測並切換到備用相機
   - 需要額外硬體成本

### 相關不變量

- INV-HW-001: 雙相機必須同時存在（寬鬆）
- INV-DATA-002: 相機序號固定

---

## RISK-015: 相機對調導致射手錯誤 🟠 HIGH

### 風險描述

若部署時將 Camera A 和 B 的物理位置對調，射手 ID 會完全錯亂。

### 技術細節

**硬編碼對應 (IRTracker.cpp:19-20):**
```cpp
cam_a = std::make_unique<BaslerCamera>(23058324);  // 應在左側
cam_b = std::make_unique<BaslerCamera>(23058325);  // 應在右側
```

**預期佈局:**
```
[Camera A]                      [Camera B]
   左側                            右側
射手 1, 2, 3                   射手 4, 5, 6
```

**若對調:**
```
[Camera B (序號 23058325)]      [Camera A (序號 23058324)]
   左側                            右側
程式以為: 射手 4, 5, 6          程式以為: 射手 1, 2, 3
實際是:   射手 1, 2, 3          實際是:   射手 4, 5, 6
```

**結果:** 所有射擊記錄的射手 ID 錯誤！

### 發生條件

- 部署時未注意相機標籤
- 維護後重新連接網路線
- 更換相機硬體但未更新程式碼
- 多套系統部署時配置混淆

### 症狀

- 射手 1 射擊被記錄為射手 4
- 所有射手 ID 錯誤（1↔4, 2↔5, 3↔6）
- 校準後仍然不準
- 歸零設定無效（因為套用到錯誤射手）

### 偵測方式

**手動驗證:**
1. 射手 1 單獨射擊
2. 檢查系統記錄的射手 ID
3. 若記錄為射手 4 → 相機對調

**自動驗證 (建議):**
```cpp
// 校準時記錄相機序號
if (calib_points_a_.size() == 4) {
    std::cerr << "Camera A calibrated (SN: 23058324)" << std::endl;
    std::cerr << "Expected position: LEFT side" << std::endl;
    std::cerr << "Please verify shooter IDs 1, 2, 3 respond correctly" << std::endl;
}
```

### 緩解措施

**當前:**
- ⚠️ **無驗證** - 完全依賴正確部署

**必須實施:**
1. **物理標籤**
   - 相機貼上清晰標籤："A - 左側 - 序號 23058324"
   - 網路線貼上標籤："Camera A"
   - 部署文件附上照片

2. **程式碼檢查**
   ```cpp
   int64_t actual_serial_a = cam_a->get_serial_number();
   if (actual_serial_a != 23058324) {
       std::cerr << "[ERROR] Camera A serial mismatch!" << std::endl;
       std::cerr << "Expected: 23058324, Got: " << actual_serial_a << std::endl;
       std::cerr << "Cameras may be swapped!" << std::endl;
       // 選項 1: 拋出例外
       // 選項 2: 自動交換 cam_a 和 cam_b 指標
   }
   ```

3. **UI 驗證模式**
   - 啟動時進入「驗證模式」
   - 提示："請射手 1 射擊" → 檢查記錄是否為射手 1
   - 依序驗證所有射手
   - 通過後才允許正式訓練

4. **配置檔案**
   ```json
   // camera_config.json
   {
     "left_camera_serial": 23058324,
     "right_camera_serial": 23058325,
     "left_shooters": [1, 2, 3],
     "right_shooters": [4, 5, 6]
   }
   ```
   - 從配置檔載入對應關係
   - 允許彈性部署
   - 支援不同硬體配置

### 相關不變量

- INV-DATA-002: 相機序號固定
- INV-COORD-001: 相機到射手映射

---

## 第五部分：系統管理風險

## RISK-009: Chrome 強制關閉遺留程序 🟡 MEDIUM

### 風險描述

系統關機時使用 `taskkill /F` 強制終止 Chrome，可能導致未儲存資料遺失或程序殘留。

### 技術細節

**當前實作 (main.cpp:387-395):**
```cpp
std::thread([]() {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    system("C:\\Windows\\System32\\taskkill.exe /F /IM chrome.exe /T ">nul 2>&1");
    std::_Exit(0);  // 直接結束，不呼叫析構函數
}).detach();
```

**問題:**
1. **強制終止** (`/F`) - Chrome 沒有機會正常關閉
2. **Detached thread** - 主程式無法控制
3. **std::_Exit(0)** - 繞過所有析構函數和 RAII 清理
4. **100ms 延遲** - 任意數值，可能不足

### 發生條件

- 使用者點擊 UI 的「關閉系統」按鈕
- HTTP 請求 `shutdown_system` action
- 系統異常需要強制關閉

### 症狀

- Chrome 設定檔損壞
- 瀏覽器歷史記錄遺失
- 背景程序殘留（chrome.exe 僵屍程序）
- 外部 PowerShell 腳本無法正確偵測關閉
- 虛擬磁碟 X: 未卸載

### 偵測方式

**Windows Task Manager:**
- 檢查是否有多個 `chrome.exe` 程序
- 檢查 CPU/記憶體使用異常

**PowerShell 監控:**
```powershell
Get-Process -Name chrome -ErrorAction SilentlyContinue
if ($?) {
    Write-Host "Chrome process still running after shutdown"
}
```

### 緩解措施

**當前:**
- ❌ 強制終止，無優雅關閉（HIGH-011 相關）

**必須實施:**
1. **Graceful Shutdown 流程**
   ```cpp
   void graceful_shutdown() {
       // 1. 停止追蹤器
       tracker.stop();  // 等待 processingLoop 執行緒結束
       
       // 2. 儲存所有設定
       saveSettings();
       
       // 3. 關閉相機
       cam_a->close();
       cam_b->close();
       
       // 4. 通知 Chrome 關閉（DevTools Protocol）
       send_chrome_close_command();
       
       // 5. 等待 Chrome 關閉（最多 5 秒）
       for (int i = 0; i < 50; i++) {
           if (!is_chrome_running()) break;
           std::this_thread::sleep_for(100ms);
       }
       
       // 6. 若仍未關閉，才使用 taskkill
       if (is_chrome_running()) {
           system("taskkill /F /IM chrome.exe");
       }
       
       // 7. 停止 HTTP 伺服器
       svr.stop();
       
       // 8. 正常退出
       std::exit(0);  // 不是 std::_Exit(0)
   }
   ```

2. **Chrome DevTools Protocol**
   ```cpp
   void send_chrome_close_command() {
       httplib::Client cli("localhost", 9222);
       auto res = cli.Post("/json/close/<tab_id>", "", "application/json");
       // Chrome 會優雅關閉該分頁
   }
   ```

3. **外部腳本通知**
   ```cpp
   // 寫入標記檔案
   std::ofstream flag("shutdown_flag.txt");
   flag << "shutdown_requested" << std::endl;
   flag.close();
   
   // PowerShell 腳本偵測此檔案並執行清理
   ```

4. **RAII 清理**
   ```cpp
   class SystemGuard {
   public:
       ~SystemGuard() {
           // 確保資源清理
           unmount_virtual_drive();
           cleanup_temp_files();
       }
   };
   
   int main() {
       SystemGuard guard;
       // ... 正常執行
   }  // 離開 scope 自動清理
   ```

### 相關不變量

- INV-FS-001: 工作目錄假設
- INV-FS-003: JSON 檔案格式有效性

---

## 第六部分：環境風險

## RISK-013: IR 環境光干擾 🟡 MEDIUM

### 風險描述

強烈的環境紅外線光（陽光、鹵素燈）可能被誤判為雷射射擊。

### 技術細節

**IR 光源:**
- 雷射筆: 850nm IR LED，聚焦點
- 陽光: 包含 700-1400nm IR 成分，散射光
- 鹵素燈: 700-3000nm IR 輻射，強烈
- 體溫: 8-14μm (遠紅外)，不影響

**偵測器:**
- Basler 相機對 700-1000nm 敏感
- 無波長過濾器 → 所有 IR 光都會偵測

**閾值過濾 (Detector.hpp:24-51):**
```cpp
cv::threshold(gray, binary, threshold_, 255, cv::THRESH_BINARY);
```
- threshold_ 預設 200（0-255 scale）
- 高亮度 IR 點會通過閾值

### 發生條件

- 室外訓練（陽光直射）
- 室內使用鹵素燈照明
- 窗戶未遮蔽（陽光斜射）
- 相機直接對準光源

### 症狀

- 大量誤偵測（每秒數十次"射擊"）
- 固定位置持續偵測（例如窗戶位置）
- 射擊記錄爆滿（latest_shots_ 迅速填滿 50 筆）
- 無法正常追蹤實際射擊

### 偵測方式

**靜態測試:**
1. 不射擊，觀察是否有偵測
2. 若有 → 環境光干擾

**動態分析:**
```cpp
// 統計偵測頻率
int detections_per_second = latest_shots_.size() / time_window;
if (detections_per_second > 10) {
    std::cerr << "[WARNING] Abnormal detection rate, possible IR interference" << std::endl;
}
```

### 緩解措施

**當前:**
- ⚠️ 僅靠閾值過濾，無波長濾鏡

**建議改進:**
1. **硬體濾鏡**
   - 加裝 850nm 窄頻帶通濾鏡
   - 阻擋其他波長 IR 光
   - 成本 ~$50/相機

2. **動態閾值調整**
   ```cpp
   int adaptive_threshold() {
       cv::Mat mean, stddev;
       cv::meanStdDev(frame, mean, stddev);
       
       // 閾值 = 平均 + 3σ
       return static_cast<int>(mean.at<double>(0) + 3 * stddev.at<double>(0));
   }
   ```

3. **時間序列過濾**
   ```cpp
   // 環境光通常持續存在，射擊是瞬時的
   if (point_exists_for_longer_than(500ms)) {
       return;  // 忽略，視為環境光
   }
   ```

4. **區域過濾**
   ```cpp
   // 若偵測點始終在同一位置（例如窗戶）
   if (point_is_stationary_for(10_detections)) {
       blacklist_region(point.x, point.y, radius=20);
   }
   ```

5. **環境建議**
   - 使用 LED 照明（較少 IR 輻射）
   - 窗簾遮蔽陽光
   - 避開陽光直射時段（上午 10-下午 3 點）
   - 背景使用深色幕布（吸收 IR）

### 相關不變量

- INV-DATA-003: 座標正規化範圍（可能超出）

---

## 第七部分：風險優先級與行動計劃

### 立即行動（1 週內）

| 風險 ID | 行動 | 負責人 | 預計時間 |
|---------|------|--------|---------|
| RISK-007 | 實作 HMAC token 認證 | 後端開發 | 8 小時 |
| RISK-008 | 加入輸入驗證函數 | 後端開發 | 6 小時 |
| RISK-002 | 加入 homography 行列式檢查 | 演算法工程師 | 2 小時 |
| RISK-015 | 加入相機序號驗證 | 後端開發 | 2 小時 |

### 短期改進（1 個月內）

| 風險 ID | 行動 | 負責人 | 預計時間 |
|---------|------|--------|---------|
| RISK-009 | 實作 graceful shutdown | 系統工程師 | 12 小時 |
| RISK-005 | 加入相機狀態 UI 顯示 | 前端開發 | 8 小時 |
| RISK-003 | 實作校準冷卻期 | 後端開發 | 4 小時 |
| RISK-006 | 優化重複偵測過濾 | 演算法工程師 | 8 小時 |

### 中期規劃（3 個月內）

| 風險 ID | 行動 | 負責人 | 預計時間 |
|---------|------|--------|---------|
| RISK-001 | 升級網路基礎設施 | 硬體工程師 | 2 天 |
| RISK-002 | 實作 9 點校準 | 演算法工程師 | 16 小時 |
| RISK-013 | 加裝 IR 濾鏡 | 硬體工程師 | 4 小時 |
| RISK-010 | 實作自動點序驗證 | 後端開發 | 8 小時 |

### 長期改善（6 個月內）

| 風險 ID | 行動 | 負責人 | 預計時間 |
|---------|------|--------|---------|
| RISK-005 | 實作熱插拔備用相機 | 硬體+軟體 | 40 小時 |
| RISK-014 | 全面記憶體分析 | 品質工程師 | 16 小時 |
| RISK-012 | 實作模糊區域處理 | 演算法工程師 | 12 小時 |
| ALL | 建立自動化測試套件 | QA 團隊 | 80 小時 |

---

## 附錄 A：風險檢查清單

### 部署前檢查

```
□ 相機序號驗證通過
□ 網路線正確連接（左 A、右 B）
□ 相機標籤清晰可見
□ Jumbo Frames 已啟用（若支援）
□ 窗簾遮蔽陽光
□ 照明使用 LED（非鹵素）
□ 背景幕布為深色
□ 認證 token 已配置
□ 防火牆規則已設定
□ 備份上一次校準檔案
```

### 訓練前檢查

```
□ 兩個相機狀態正常（綠色）
□ 校準有效（距離上次校準 < 7 天）
□ 射擊記錄目錄存在且可寫
□ 磁碟空間充足（> 1GB）
□ 系統時間正確
□ 環境光干擾測試通過（無誤偵測）
□ 射手區域清楚標示
```

### 每週檢查

```
□ 檢查錯誤日誌（error.log, t91_debug.log）
□ 檢查 grab 錯誤頻率（< 5%）
□ 檢查重複射擊過濾率（< 10%）
□ 檢查磁碟空間
□ 備份射擊成績資料
□ 清理臨時檔案
□ 更新系統軟體（若有）
```

---

## 附錄 B：故障排除指南

### 問題：所有射手都無回應

**可能原因:** RISK-005 (兩個相機都失敗)

**檢查步驟:**
1. 檢查 GigE 網路線連接
2. 檢查相機電源（PoE 或外部）
3. 重新啟動系統
4. 檢查 Pylon Viewer 是否能偵測相機
5. 重新安裝 Pylon SDK 驅動

---

### 問題：射手 1-3 或 4-6 無回應

**可能原因:** RISK-005 (單一相機失敗)

**檢查步驟:**
1. 確認是哪個相機失敗（Console 訊息）
2. 檢查該相機的網路線
3. 重新啟動該相機（拔插電源）
4. 檢查相機序號是否正確

---

### 問題：射擊記錄的射手 ID 錯誤

**可能原因:** RISK-015 (相機對調)

**檢查步驟:**
1. 檢查相機物理位置（左 A、右 B）
2. 檢查相機序號（程式碼 vs 實際）
3. 若對調，交換網路線或更新程式碼
4. 重新校準

---

### 問題：大量誤偵測

**可能原因:** RISK-013 (環境光干擾)

**檢查步驟:**
1. 不射擊，觀察是否有偵測
2. 檢查窗戶是否遮蔽
3. 檢查照明類型（避免鹵素燈）
4. 調高 threshold 值（200 → 220）
5. 考慮加裝 IR 濾鏡

---

### 問題：系統無法關閉

**可能原因:** RISK-009 (Chrome 程序殘留)

**檢查步驟:**
1. 開啟 Task Manager
2. 手動終止所有 chrome.exe 和 t91_tracker.exe
3. 檢查虛擬磁碟是否已卸載
4. 重新啟動電腦（若必要）

---

**文件結束**

此文件符合 AI-Assisted Development Constitution v2.0 Article 9 要求。  
**定期審查已知風險，優先處理 CRITICAL 和 HIGH 等級風險。**
