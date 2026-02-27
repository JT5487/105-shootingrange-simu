# T91 紅外線射擊追蹤系統 - 系統不變量 (Invariants)

**版本:** 1.0  
**日期:** 2026-02-05  
**狀態:** 初始版本  
**依據:** AI-Assisted Development Constitution v2.0 Article 6

---

## 文件目的

本文件記錄 T91 系統的**所有不變量 (Invariants)**。

**不變量**是系統中**絕對不可違反**的規則，無論任何情況下都必須維持。根據憲章 Article 6：

> "If a feature request conflicts with any system invariant:  
> DECISION: Feature MUST be modified or rejected. Invariant MUST NOT be compromised."

**重要性:**
- 不變量優先於功能需求
- 違反不變量會導致系統錯誤或安全漏洞
- 所有開發者必須熟知並遵守

---

## 不變量分類

本文件將不變量分為以下類別：

1. **資料完整性不變量** - 資料範圍與有效性
2. **並發安全不變量** - 執行緒安全與同步
3. **校準不變量** - 相機校準規則
4. **座標系統不變量** - 空間轉換規則
5. **狀態一致性不變量** - 系統狀態轉換
6. **安全性不變量** - 安全與存取控制
7. **硬體不變量** - 硬體配置假設
8. **檔案系統不變量** - 檔案路徑與權限

---

## 1. 資料完整性不變量

### INV-DATA-001: 射手 ID 範圍

**規則:** 射手 ID 必須在 [1, 6] 範圍內

**原因:** 系統設計支援 6 個射手，陣列大小為 6

**強制執行位置:**
- `main.cpp:161-163` (register_hit)
- `main.cpp:239-247` (knockdown_target, reset_target)
- `IRTracker.cpp:327-333` (setShooterIntensity)
- `IRTracker.cpp:335-341` (setShooterMinIntensity)
- `IRTracker.cpp:343-349` (setShooterZeroing)

**驗證方法:**
```cpp
if (shooter_id < 1 || shooter_id > 6) {
    // 拒絕操作，回傳錯誤
    return false;
}
```

**違反後果:**
- 陣列越界存取 (hits_[shooter_id-1])
- 記憶體損壞或 segmentation fault
- 不可預測的系統行為

---

### INV-DATA-002: 相機序號固定

**規則:** 系統必須使用以下兩個 Basler 相機

| 相機 | 序號 | 責任射手 | 位置 |
|------|------|---------|------|
| Camera A | 23058324 | 1, 2, 3 | 左側 |
| Camera B | 23058325 | 4, 5, 6 | 右側 |

**強制執行位置:**
- `IRTracker.cpp:19-20` (硬編碼於建構子)

**相關程式碼:**
```cpp
cam_a = std::make_unique<BaslerCamera>(23058324);
cam_b = std::make_unique<BaslerCamera>(23058325);
```

**違反後果:**
- 相機初始化失敗
- 射手 ID 對應錯誤（左右相機對調）
- 校準資料無效

**備註:**
- 此為臨時硬編碼（HIGH-007 技術債）
- 未來應改為從配置檔載入
- 但在配置系統完成前，此不變量必須維持

---

### INV-DATA-003: 座標正規化範圍

**規則:** 所有正規化座標必須在 [-1.0, 1.0] 範圍內

**涵蓋變數:**
- `dst_points_` (IRTracker.hpp:134-139) - 目標螢幕角落座標
- `transformCoordinate()` 輸出 - 轉換後的命中座標
- `latest_shots_` 中的 x, y 欄位

**座標系統定義:**
```
Y軸 (+1.0)
    │
    │  標靶區域
    │
────┼──── X軸
-1.0│ 0  +1.0
    │
    │
   (-1.0)
```

**強制執行:**
- Homography 轉換後的座標應自動落在此範圍
- 若超出範圍，視為無效射擊（可能是雜訊）

**驗證方法:**
```cpp
bool isValidCoordinate(float x, float y) {
    return (x >= -1.0 && x <= 1.0 && y >= -1.0 && y <= 1.0);
}
```

---

### INV-DATA-004: 校準點數量

**規則:** 每個相機必須恰好收集 4 個校準點

**校準點順序:** 左上 → 右上 → 右下 → 左下 (TL → TR → BR → BL)

**強制執行位置:**
- `IRTracker.cpp:468` (Camera A 檢查: `calib_points_a_.size() == 4`)
- `IRTracker.cpp:631` (Camera B 檢查: `calib_points_b_.size() == 4`)

**原因:**
- OpenCV `cv::findHomography()` 要求至少 4 點
- 4 點定義一個透視變換的最小集合

**違反後果:**
- `cv::findHomography()` 失敗
- 無法進行座標轉換
- 系統無法正常追蹤

---

### INV-DATA-005: 射擊記錄緩衝區上限

**規則:** `latest_shots_` 向量大小不得超過 50

**強制執行位置:**
- `IRTracker.cpp:612-613`

**相關程式碼:**
```cpp
if (latest_shots_.size() > 50) {
    latest_shots_.erase(latest_shots_.begin());
}
```

**原因:**
- 記憶體限制
- 避免無限制增長
- 維持即時性能

**備註:**
- 數值 50 為魔術數字（HIGH-006）
- 應命名為常數：`MAX_SHOTS_BUFFER = 50`

---

## 2. 並發安全不變量

### INV-CONCUR-001: 射擊資料存取保護

**規則:** 所有 `latest_shots_` 存取必須持有 `shots_mutex_`

**保護對象:**
- `std::vector<ShotResult> latest_shots_` (IRTracker.hpp:142)

**讀取操作 (需鎖定):**
- `IRTracker::getLatestShots()` (IRTracker.cpp:257)
- `processingLoop()` 中的重複偵測 (IRTracker.cpp:581-616)

**寫入操作 (需鎖定):**
- `processingLoop()` 加入新射擊 (IRTracker.cpp:605, 650)
- `processingLoop()` 移除舊項目 (IRTracker.cpp:612)

**鎖定模式:**
```cpp
std::lock_guard<std::mutex> lock(shots_mutex_);
// ... 存取 latest_shots_
```

**違反後果:**
- 資料競爭 (data race)
- HTTP 請求讀取到不一致的資料
- 可能導致 segmentation fault

---

### INV-CONCUR-002: 狀態資料存取保護

**規則:** 所有 `hits_` 和 `targets_down_` 存取必須持有 `status_mutex_`

**保護對象:**
- `int hits_[6]` (IRTracker.hpp:144)
- `bool targets_down_[6]` (IRTracker.hpp:145)

**存取位置 (已正確鎖定):**
- `registerHit()` (IRTracker.cpp:163)
- `knockdownTarget()` (IRTracker.cpp:286)
- `resetTarget()` (IRTracker.cpp:400)
- `isTargetDown()` (IRTracker.cpp:407)
- `getHitCount()` (IRTracker.cpp:415)
- `resetAllHits()` (IRTracker.cpp:424)
- `getShooterHits()` (IRTracker.cpp:432)
- `processingLoop()` 更新命中 (IRTracker.cpp:443)

**鎖定模式:**
```cpp
std::lock_guard<std::mutex> lock(status_mutex_);
// ... 存取 hits_ 或 targets_down_
```

---

### INV-CONCUR-003: 校準資料存取保護

**規則:** 所有校準相關資料存取必須持有 `calib_mutex_`

**保護對象:**
- `std::vector<cv::Point2f> calib_points_a_` (IRTracker.hpp:136)
- `std::vector<cv::Point2f> calib_points_b_` (IRTracker.hpp:137)
- `cv::Mat homography_a_` (IRTracker.hpp:140)
- `cv::Mat homography_b_` (IRTracker.hpp:141)
- `std::string calibration_mode_` (IRTracker.hpp:148)

**存取位置 (已正確鎖定):**
- `startCalibrationA/B()` (IRTracker.cpp:172, 278)
- `processingLoop()` 校準流程 (IRTracker.cpp:467, 630)

**鎖定模式:**
```cpp
std::lock_guard<std::mutex> lock(calib_mutex_);
// ... 存取校準資料
```

---

### INV-CONCUR-004: 禁止嵌套鎖定

**規則:** 永遠不可同時持有兩個以上的 mutex

**原因:** 避免死鎖 (deadlock)

**當前狀態:** ✅ 符合（三個 mutex 從未嵌套使用）

**驗證方法:**
- 程式碼審查確認無嵌套 `std::lock_guard`
- 使用 Thread Sanitizer 動態檢測

**若未來需要多個鎖:**
- 使用 `std::scoped_lock` (C++17) 原子性鎖定多個 mutex
- 或定義全域鎖定順序（例如：always status_mutex_ before shots_mutex_）

---

## 3. 校準不變量

### INV-CALIB-001: 校準點對應順序

**規則:** 校準點必須按照固定順序對應螢幕角落

**順序定義:**
```
點 1: 左上角 (Top-Left)      dst_points_[0] = (-0.85, 0.85)
點 2: 右上角 (Top-Right)     dst_points_[1] = (0.85, 0.85)
點 3: 右下角 (Bottom-Right)  dst_points_[2] = (0.85, -0.85)
點 4: 左下角 (Bottom-Left)   dst_points_[3] = (-0.85, -0.85)
```

**強制執行位置:**
- `IRTracker.hpp:134-139` (dst_points_ 定義)
- `IRTracker.cpp:501-506` (Camera A homography 計算)
- `IRTracker.cpp:664-669` (Camera B homography 計算)

**使用者操作要求:**
- 前端 UI 必須引導使用者依序點擊四個角落
- 順序錯誤會導致投影扭曲

**驗證方法:**
- 目前**無自動驗證**
- 使用者視覺檢查：校準後射擊已知位置，驗證命中點正確

---

### INV-CALIB-002: 校準資料必須持久化

**規則:** 每次校準完成後，必須立即儲存到檔案

**檔案路徑:**
- Camera A: `calibration_a.json`
- Camera B: `calibration_b.json`

**儲存時機:**
- `IRTracker.cpp:506` (Camera A homography 計算後)
- `IRTracker.cpp:669` (Camera B homography 計算後)

**檔案格式 (JSON):**
```json
{
  "calibration": {
    "valid": true,
    "points": [
      [px1, py1],  // 相機座標
      [px2, py2],
      [px3, py3],
      [px4, py4]
    ],
    "homography": [
      [h11, h12, h13],
      [h21, h22, h23],
      [h31, h32, h33]
    ]
  }
}
```

**違反後果:**
- 重啟後需重新校準
- 無法恢復先前的校準狀態
- 浪費訓練時間

---

### INV-CALIB-003: Homography 矩陣大小

**規則:** Homography 矩陣必須是 3×3 浮點數矩陣

**資料型別:** `cv::Mat` (CV_32F or CV_64F)

**驗證:**
```cpp
assert(homography_a_.rows == 3 && homography_a_.cols == 3);
assert(homography_a_.type() == CV_32F || homography_a_.type() == CV_64F);
```

**來源:** `cv::findHomography()` 保證回傳 3×3 矩陣

---

### INV-CALIB-004: 校準模式互斥

**規則:** 同一時間只能有一個相機處於校準模式

**有效值:** `calibration_mode_` ∈ {"none", "a", "b"}

**狀態轉換:**
```
"none" → "a"  (呼叫 startCalibrationA())
"none" → "b"  (呼叫 startCalibrationB())
"a" → "none"  (完成 4 點校準)
"b" → "none"  (完成 4 點校準)
```

**禁止轉換:**
- "a" → "b" (必須先完成或取消 A)
- "b" → "a" (必須先完成或取消 B)

**強制執行位置:**
- `startCalibrationA()` / `startCalibrationB()` 檢查當前狀態
- 前端 UI 停用另一個相機的校準按鈕

---

## 4. 座標系統不變量

### INV-COORD-001: 相機到射手映射

**規則:** 相機與射手 ID 的對應關係固定

| 相機 | 負責射手 | X 座標範圍 (正規化) |
|------|---------|---------------------|
| Camera A | 1, 2, 3 | [-1.0, 0.0] (左半) |
| Camera B | 4, 5, 6 | [0.0, +1.0] (右半) |

**強制執行位置:**
- `IRTracker.cpp:536-541` (Camera A 處理)
- `IRTracker.cpp:686-691` (Camera B 處理)

**射手 ID 計算公式:**
```cpp
// Camera A (3 zones)
int shooter_id = static_cast<int>((pt.x + 1.0) / (2.0 / 3.0)) + 1;
if (shooter_id < 1) shooter_id = 1;
if (shooter_id > 3) shooter_id = 3;

// Camera B (3 zones)
int shooter_id = static_cast<int>((pt.x + 1.0) / (2.0 / 3.0)) + 4;
if (shooter_id < 4) shooter_id = 4;
if (shooter_id > 6) shooter_id = 6;
```

**物理假設:**
- 相機 A 實體位於靶場左側
- 相機 B 實體位於靶場右側
- **若相機物理位置對調，此不變量會違反！**

**驗證方法:**
- 系統啟動時檢查相機序號與位置
- 未來應在 UI 顯示警告（若相機對調）

---

### INV-COORD-002: 歸零偏移加法順序

**規則:** 歸零偏移必須按照以下順序累加

**優先順序 (低 → 高):**
1. **全域歸零** (global_zeroing_x_, global_zeroing_y_)
2. **模式歸零** (mode_zeroing_x_, mode_zeroing_y_) - 依距離 (25m/75m/175m/300m)
3. **個別射手歸零** (shooter_zeroing_) - 6 個射手各自的偏移

**應用公式:**
```cpp
float effective_x = pt.x + global_zeroing_x_ + mode_zeroing_x_ + shooter_zeroing_[shooter_id-1].x;
float effective_y = pt.y + global_zeroing_y_ + mode_zeroing_y_ + shooter_zeroing_[shooter_id-1].y;
```

**強制執行位置:**
- `IRTracker.cpp:518-563` (Camera A)
- `IRTracker.cpp:674-717` (Camera B)

**原因:**
- 全域偏移：修正整體系統誤差
- 模式偏移：修正不同距離的彈道下墜
- 射手偏移：修正個別步槍的歸零偏差

**單位:** 正規化座標 (無量綱)，典型值 -0.1 ~ +0.1

---

### INV-COORD-003: 射手區域等寬劃分

**規則:** 每個射手的 X 軸區域寬度相等

**區域寬度:** `zone_width = 2.0 / 6.0 ≈ 0.333`

**射手 X 軸範圍:**
| 射手 | X 範圍 (正規化) | 中心 X |
|------|----------------|--------|
| 1 | [-1.00, -0.67) | -0.83 |
| 2 | [-0.67, -0.33) | -0.50 |
| 3 | [-0.33, 0.00) | -0.17 |
| 4 | [0.00, 0.33) | 0.17 |
| 5 | [0.33, 0.67) | 0.50 |
| 6 | [0.67, 1.00] | 0.83 |

**強制執行位置:**
- `IRTracker.cpp:359` (zone_width 計算)
- `IRTracker.cpp:536-541` (Camera A 射手 ID 計算)
- `IRTracker.cpp:686-691` (Camera B 射手 ID 計算)

**備註:**
- 邊界值屬於右側射手（例如 x=-0.67 屬於射手 2）
- 數值 2.0 和 6.0 為魔術數字（HIGH-006）

---

### INV-COORD-004: 垂直容差依距離變化

**規則:** 命中判定的垂直容差必須根據距離模式調整

**容差值 (正規化座標):**
| 距離模式 | 垂直容差 | 原因 |
|---------|---------|------|
| 25m | 0.15 | 近距離，標靶大，容差中等 |
| 75m | 0.10 | 中距離，標靶中，容差較緊 |
| 175m | 0.20 | 遠距離，標靶小，容差較鬆 |
| 300m | 0.25 | 超遠距離，標靶極小，容差最鬆 |
| dynamic | (無容差檢查) | 動態模式，所有射擊有效 |

**強制執行位置:**
- `IRTracker.cpp:372-380` (Camera A)
- `IRTracker.cpp:697-705` (Camera B)

**計算方式:**
```cpp
float vertical_center = /* 根據射手和目標調整計算 */;
bool hit = fabs(adjusted_y - vertical_center) < vertical_tolerance;
```

**備註:**
- 容差值為經驗值（HIGH-006 魔術數字）
- 應基於實際射擊資料調校
- 未來可改為可配置參數

---

## 5. 狀態一致性不變量

### INV-STATE-001: 標靶倒下與命中計數同步

**規則:** 標靶倒下 (`targets_down_[i]`) 必須與命中計數 (`hits_[i]`) 一致

**一致性規則:**
```
targets_down_[i] == true  ⇒  hits_[i] > 0
targets_down_[i] == false  ⇒  hits_[i] >= 0  (可以有命中但標靶站立)
```

**狀態轉換:**
1. **擊倒標靶:** `knockdownTarget(shooter_id)`
   - `targets_down_[shooter_id-1] = true`
   - `hits_[shooter_id-1]` 不變（保留計數）

2. **重置標靶:** `resetTarget(shooter_id)`
   - `targets_down_[shooter_id-1] = false`
   - `hits_[shooter_id-1] = 0` （清除計數）

3. **註冊命中:** `registerHit(shooter_id)` 或 processingLoop 自動偵測
   - `hits_[shooter_id-1]++`
   - `targets_down_[shooter_id-1]` 不變

**驗證方法:**
```cpp
// 不變量檢查函數
bool checkStateConsistency() {
    for (int i = 0; i < 6; i++) {
        if (targets_down_[i] && hits_[i] == 0) {
            return false;  // 違反不變量
        }
    }
    return true;
}
```

---

### INV-STATE-002: 校準模式與追蹤互斥

**規則:** 校準模式下不應進行命中判定

**狀態檢查:**
```cpp
if (calibration_mode_ != "none") {
    // 僅收集校準點，不執行命中判定
}
```

**強制執行位置:**
- `IRTracker.cpp:461-509` (Camera A 校準)
- `IRTracker.cpp:625-672` (Camera B 校準)

**原因:**
- 校準點擊可能誤判為射擊
- 避免污染射擊記錄

**當前實作:**
- 使用 `if-else` 分支確保互斥
- ✅ 符合不變量

---

### INV-STATE-003: 射擊記錄時間戳遞增

**規則:** `latest_shots_` 中的射擊必須按時間順序排列

**時間戳來源:** `std::chrono::steady_clock::now()`

**保證:**
```cpp
for (size_t i = 1; i < latest_shots_.size(); i++) {
    assert(latest_shots_[i].timestamp >= latest_shots_[i-1].timestamp);
}
```

**強制執行:**
- 新射擊總是加入向量尾端 (`push_back`)
- 移除時從頭部移除 (`erase(begin())`)

**用途:**
- 重複射擊偵測需要時間序
- HTTP 回應保持時間順序

---

## 6. 安全性不變量

### INV-SEC-001: HTTP 端點需要認證 (⚠️ 當前違反)

**規則:** 敏感操作必須要求認證

**敏感操作清單:**
- `shutdown_system` - 系統關機
- `adjust_targets` - 修改目標設定
- `start_calibration_a/b` - 開始校準

**當前狀態:** ❌ **CRITICAL-007 違反**
- 所有端點完全無認證
- 任何網路用戶端可執行所有操作

**必要修正:**
```cpp
// 所有敏感操作前檢查
if (!verifyAuthToken(req.get_header_value("X-Auth-Token"))) {
    res.status = 403;
    res.set_content(json({{"error", "Unauthorized"}}).dump(), "application/json");
    return;
}
```

**認證方案:**
- HMAC token 驗證
- Token 儲存於 `config/auth_token.txt`
- 每次請求附帶 `X-Auth-Token` header

---

### INV-SEC-002: 輸入必須經過驗證 (⚠️ 當前違反)

**規則:** 所有 HTTP 輸入必須經過型別與範圍驗證

**當前問題:** ❌ **HIGH-013 違反**
- 直接使用 `.get<T>()` 無 try-catch
- 無範圍檢查（例如 scale 可能是負數或極大值）

**必要修正:**
```cpp
// 範例：安全取得 double 值
double safe_get_double(const json& j, const char* key, double min, double max, double default_val) {
    try {
        if (!j.contains(key)) return default_val;
        double val = j[key].get<double>();
        if (val < min || val > max) {
            throw std::out_of_range("Value out of range");
        }
        return val;
    } catch (...) {
        return default_val;
    }
}
```

**驗證規則:**
| 參數 | 型別 | 範圍 |
|------|------|------|
| shooter_id | int | [1, 6] |
| zeroingX | double | [-100.0, 100.0] |
| zeroingY | double | [-100.0, 100.0] |
| scale | double | [0.1, 10.0] |
| threshold | int | [0, 255] |

---

### INV-SEC-003: 禁止路徑遍歷

**規則:** 檔案路徑必須經過驗證，禁止 `..` 或絕對路徑

**當前狀態:** ⚠️ 部分符合
- 檔案路徑為硬編碼（不接受使用者輸入）
- 但硬編碼使用 `../射擊成績/` (IRTracker.cpp:219)

**風險:**
- 若未來允許使用者指定檔案名稱，可能遭受路徑遍歷攻擊

**必要檢查 (若接受使用者輸入):**
```cpp
bool isValidFilename(const std::string& filename) {
    return filename.find("..") == std::string::npos &&
           filename.find("/") == std::string::npos &&
           filename.find("\\") == std::string::npos;
}
```

---

## 7. 硬體不變量

### INV-HW-001: 雙相機必須同時存在

**規則:** 系統需要兩個 Basler 相機才能正常運作

**初始化檢查:**
- `IRTracker.cpp:22-46` 嘗試開啟兩個相機

**當前行為:**
- 若相機 A 或 B 開啟失敗，印出錯誤訊息
- **但系統仍繼續執行**（IRTracker.cpp:130-135）

**實際不變量 (寬鬆):**
- 至少一個相機必須成功開啟
- 若兩個都失敗，系統應拒絕啟動

**建議強化:**
```cpp
if (!cam_a_opened && !cam_b_opened) {
    throw std::runtime_error("No cameras available, cannot start");
}
```

---

### INV-HW-002: 相機幀率固定

**規則:** 相機必須設定為 45 fps

**強制執行位置:**
- `CameraManager.cpp:115` (Basler 相機設定)

**原因:**
- 平衡影像品質與網路頻寬
- 避免 GigE 網路封包遺失

**驗證:**
```cpp
camera->FrameRate.SetValue(45.0);
```

**風險:**
- 若超過 60 fps，可能導致網路壅塞（RISK-001）

---

### INV-HW-003: GigE 網路專用

**規則:** Basler 相機必須連接到專用 GigE 網路卡

**網路假設:**
- 1 Gbps 乙太網路
- MTU size ≥ 1500 bytes
- 低延遲 (< 1ms)

**頻寬計算:**
- 2 相機 × 45 fps × ~2 MB/幀 ≈ 180 MB/s
- 小於 1 Gbps (125 MB/s) 理論上限

**建議配置:**
- 相機專用網路卡（不與其他裝置共享）
- Jumbo Frames (MTU 9000) 可提升效能

---

## 8. 檔案系統不變量

### INV-FS-001: 工作目錄假設

**規則:** 系統執行時，工作目錄必須是專案根目錄

**預期結構:**
```
專案根目錄/
├── t91_tracker.exe        (可執行檔)
├── calibration_a.json     (相機 A 校準)
├── calibration_b.json     (相機 B 校準)
├── target_settings.json   (目標設定)
├── last_results.json      (最後結果)
├── t91_debug.log          (除錯日誌)
└── ../射擊成績/            (射擊記錄資料夾)
    └── 成績_*.csv
```

**相對路徑依賴:**
- `calibration_a.json` (IRTracker.cpp:99)
- `../射擊成績/成績_...csv` (IRTracker.cpp:219)

**驗證方法:**
- 啟動時檢查關鍵檔案是否存在
- 若路徑不正確，印出警告並使用預設值

---

### INV-FS-002: 射擊記錄目錄必須存在

**規則:** `../射擊成績/` 目錄必須在第一次儲存前存在

**當前實作:** ⚠️ **無自動建立**
- `IRTracker.cpp:182` 直接開啟 CSV 檔案
- 若目錄不存在，`std::ofstream` 會失敗

**建議修正:**
```cpp
#include <filesystem>

std::filesystem::create_directories("../射擊成績");
```

**權限要求:**
- 程式必須具備讀寫權限

---

### INV-FS-003: JSON 檔案格式有效性

**規則:** 所有 JSON 檔案必須是 UTF-8 編碼且格式正確

**載入錯誤處理:**
- `main.cpp:42-70` (target_settings.json)
- `IRTracker.cpp:97-166` (calibration_a/b.json)

**當前行為:**
- 若檔案不存在或格式錯誤，使用預設值
- 印出錯誤訊息到 console

**預設值:**
- target_settings: 空 JSON 物件 `{}`
- calibration: 無校準資料（需重新校準）

---

## 9. 不變量違反偵測與處理

### 9.1 編譯時檢查

**使用 static_assert:**
```cpp
static_assert(sizeof(hits_) / sizeof(hits_[0]) == 6, 
              "hits_ array must have exactly 6 elements");
```

### 9.2 執行時檢查

**使用 assert (Debug 模式):**
```cpp
assert(shooter_id >= 1 && shooter_id <= 6);
```

**使用例外 (Production 模式):**
```cpp
if (shooter_id < 1 || shooter_id > 6) {
    throw std::invalid_argument("shooter_id out of range [1,6]");
}
```

### 9.3 不變量違反時的行動

| 嚴重性 | 行動 |
|--------|------|
| **CRITICAL** | 立即終止程式 (`std::terminate`) |
| **HIGH** | 拋出例外，記錄日誌 |
| **MEDIUM** | 回傳錯誤碼，記錄警告 |
| **LOW** | 記錄警告，繼續執行 |

---

## 10. 不變量審查與更新

**審查頻率:**
- 每次新增功能前
- 每次修改核心邏輯後
- 每季度全面審查

**更新觸發條件:**
- 發現新的隱含不變量
- 硬體或外部依賴變更
- 安全性要求變更

**文件所有者:** 架構師 / 技術負責人

---

## 11. 附錄：不變量快速參考表

| ID | 類別 | 簡述 | 狀態 |
|----|------|------|------|
| INV-DATA-001 | 資料 | 射手 ID ∈ [1,6] | ✅ 強制執行 |
| INV-DATA-002 | 資料 | 相機序號固定 (A=23058324, B=23058325) | ✅ 硬編碼 |
| INV-DATA-003 | 資料 | 座標 ∈ [-1.0, 1.0] | ✅ 自然保證 |
| INV-DATA-004 | 資料 | 校準點 = 4 個 | ✅ 強制執行 |
| INV-DATA-005 | 資料 | latest_shots_.size() ≤ 50 | ✅ 強制執行 |
| INV-CONCUR-001 | 並發 | shots_mutex_ 保護 latest_shots_ | ✅ 正確使用 |
| INV-CONCUR-002 | 並發 | status_mutex_ 保護 hits_/targets_down_ | ✅ 正確使用 |
| INV-CONCUR-003 | 並發 | calib_mutex_ 保護校準資料 | ✅ 正確使用 |
| INV-CONCUR-004 | 並發 | 禁止嵌套鎖定 | ✅ 符合 |
| INV-CALIB-001 | 校準 | 校準點順序 (TL→TR→BR→BL) | ⚠️ 無自動驗證 |
| INV-CALIB-002 | 校準 | 校準資料持久化 | ✅ 強制執行 |
| INV-CALIB-003 | 校準 | Homography 矩陣 3×3 | ✅ OpenCV 保證 |
| INV-CALIB-004 | 校準 | 校準模式互斥 | ✅ 強制執行 |
| INV-COORD-001 | 座標 | 相機-射手對應 (A=1-3, B=4-6) | ✅ 強制執行 |
| INV-COORD-002 | 座標 | 歸零偏移加法順序 | ✅ 強制執行 |
| INV-COORD-003 | 座標 | 射手區域等寬劃分 | ✅ 強制執行 |
| INV-COORD-004 | 座標 | 垂直容差依距離變化 | ✅ 強制執行 |
| INV-STATE-001 | 狀態 | 標靶倒下與命中同步 | ✅ 邏輯正確 |
| INV-STATE-002 | 狀態 | 校準與追蹤互斥 | ✅ 分支正確 |
| INV-STATE-003 | 狀態 | 射擊時間戳遞增 | ✅ 自然保證 |
| INV-SEC-001 | 安全 | HTTP 認證要求 | ❌ **CRITICAL-007** |
| INV-SEC-002 | 安全 | 輸入驗證要求 | ❌ **HIGH-013** |
| INV-SEC-003 | 安全 | 禁止路徑遍歷 | ⚠️ 部分符合 |
| INV-HW-001 | 硬體 | 雙相機存在 | ⚠️ 寬鬆檢查 |
| INV-HW-002 | 硬體 | 相機幀率 45fps | ✅ 強制設定 |
| INV-HW-003 | 硬體 | GigE 網路專用 | ℹ️ 部署要求 |
| INV-FS-001 | 檔案 | 工作目錄假設 | ⚠️ 無驗證 |
| INV-FS-002 | 檔案 | 射擊記錄目錄存在 | ⚠️ 無自動建立 |
| INV-FS-003 | 檔案 | JSON 格式有效性 | ✅ 錯誤處理 |

**圖例:**
- ✅ 符合且已強制執行
- ⚠️ 部分符合或需改進
- ❌ 違反不變量（需修正）
- ℹ️ 文件化要求（非程式碼強制）

---

**文件結束**

此文件符合 AI-Assisted Development Constitution v2.0 Article 6 要求。  
**所有不變量優先於功能需求，不可妥協。**
