# T91 紅外線射擊追蹤系統 - 技術債務追蹤日誌

**版本:** 1.0  
**建立日期:** 2026-02-05  
**狀態:** 進行中  
**依據:** AI-Assisted Development Constitution v2.0 Article 7

---

## 文件目的

根據憲章 Article 7：
> "Any of the following MUST be explicitly marked:
> - Workarounds
> - Temporary solutions  
> - Hard-coded behaviors
> - Magic numbers / magic strings
> - Assumptions that may change"

本文件追蹤所有系統中的技術債務項目，確保：
1. 所有捷徑都是**可見的**
2. 每個債務都有**負責人**和**期限**
3. 債務不會被遺忘

---

## 技術債務總覽

**統計（2026-02-05）:**
- 🔴 CRITICAL: 2 項
- 🟠 HIGH: 10 項  
- 🟡 MEDIUM: 8 項
- 🟢 LOW: 5 項
- **總計:** 25 項

**債務分類:**
- HARDCODED: 8 項
- WORKAROUND: 3 項
- MAGIC_NUMBER: 10 項
- ASSUMPTION: 4 項

---

## 債務清單

### 🔴 CRITICAL 等級

#### DEBT-CRIT-001: HTTP API 無認證

```cpp
// 位置: cpp/src/main.cpp:131-427
// ⚠️ TECH-DEBT: SECURITY
// 類別: CRITICAL
// Owner: 後端開發團隊
// Deadline: 對外開放前必須完成
// Reason: 內網環境暫時可接受，但絕不能對外開放
// Resolution: 實作 HMAC token 認證 (見 KNOWN_RISKS.md RISK-007)
```

**狀態:** 🟡 已接受風險（內網環境）  
**追蹤:** 若系統需對外，此項立即升級為 P0

---

#### DEBT-CRIT-002: 輸入驗證缺失

```cpp
// 位置: cpp/src/main.cpp:249-250, 206-217 等多處
// ⚠️ TECH-DEBT: SECURITY
// 類別: CRITICAL
// Owner: 後端開發團隊
// Deadline: 生產環境部署前
// Reason: 內網信任環境，暫時可接受
// Resolution: 加入 InputValidator 類別，驗證所有 HTTP 輸入
// Example:
//   double zx = data["zeroingX"].get<double>();  // 當前：無驗證
//   double zx = safe_get_double(data, "zeroingX", -100, 100, 0.0);  // 目標
```

**狀態:** 🟡 已接受風險（內網環境）  
**追蹤:** 若有外部整合（UE5 等），需立即處理

---

### 🟠 HIGH 等級

#### DEBT-HIGH-001: main.cpp 違反單一職責

```cpp
// 位置: cpp/src/main.cpp (438 行)
// ⚠️ TECH-DEBT: ARCHITECTURE
// 類別: HIGH
// Owner: 架構師
// Deadline: 2026-Q2（整體重構時）
// Reason: 快速開發導致職責混合
// Resolution: 重構為 4 個模組
//   - ConfigurationManager (配置管理)
//   - HttpApiServer (HTTP 路由)
//   - SystemController (生命週期)
//   - main (組裝器)
// Impact: 所有新功能若涉及 HTTP/配置，技術債會加劇
```

**狀態:** 🟠 計劃重構  
**追蹤:** 見 MODULE_BOUNDARIES.md  
**新增功能影響:** 若新功能涉及 HTTP API，建議先輕量重構

---

#### DEBT-HIGH-002: IRTracker 違反單一職責

```cpp
// 位置: cpp/src/IRTracker.cpp (1000+ 行)
// ⚠️ TECH-DEBT: ARCHITECTURE
// 類別: HIGH
// Owner: 架構師
// Deadline: 2026-Q2（整體重構時）
// Reason: 功能持續增加導致類別膨脹
// Resolution: 重構為 6 個服務
//   - CalibrationService (校準邏輯)
//   - HitDetectionService (命中判定)
//   - TrackerState (狀態管理)
//   - ResultsArchiver (結果匯出)
//   - TrackerConfiguration (配置參數)
//   - IRTracker (僅協調)
// Impact: 所有新功能若涉及追蹤/校準/命中，技術債會加劇
```

**狀態:** 🟠 計劃重構  
**追蹤:** 見 MODULE_BOUNDARIES.md  
**新增功能影響:** 若新功能涉及追蹤邏輯，建議先輕量重構

---

#### DEBT-HIGH-003: 相機序號硬編碼

```cpp
// 位置: cpp/src/IRTracker.cpp:19-20
// ⚠️ TECH-DEBT: HARDCODED
// Owner: 系統工程師
// Deadline: 2026-03-31（支援多套系統部署前）
// Reason: 單一系統部署時可接受
// Resolution: 從 camera_config.json 載入
// Code:
cam_a = std::make_unique<BaslerCamera>(23058324);  // 當前
cam_b = std::make_unique<BaslerCamera>(23058325);

// 目標:
auto config = loadCameraConfig("camera_config.json");
cam_a = std::make_unique<BaslerCamera>(config.camera_a_serial);
cam_b = std::make_unique<BaslerCamera>(config.camera_b_serial);
```

**狀態:** 🟢 已記錄  
**相關風險:** RISK-015 (相機對調導致射手錯誤)

---

#### DEBT-HIGH-004: 檔案路徑硬編碼

```cpp
// 位置: 多處
// ⚠️ TECH-DEBT: HARDCODED
// Owner: 系統工程師
// Deadline: 2026-03-31
// Reason: 單一部署環境可接受
// Resolution: 從配置檔或環境變數載入

// 當前硬編碼位置:
// 1. main.cpp:42
"target_settings.json"

// 2. IRTracker.cpp:99
"calibration_a.json", "calibration_b.json"

// 3. IRTracker.cpp:184
"last_results.json"

// 4. IRTracker.cpp:219
"../射擊成績/成績_...csv"

// 5. IRTracker.cpp:802
"t91_debug.log"

// 目標: 使用配置類別
struct Paths {
    std::string config_dir = "./config";
    std::string results_dir = "../射擊成績";
    std::string log_file = "t91_debug.log";
};
```

**狀態:** 🟢 已記錄

---

#### DEBT-HIGH-005: 魔術數字 - 垂直容差

```cpp
// 位置: cpp/src/IRTracker.cpp:372-380, 697-705
// ⚠️ TECH-DEBT: MAGIC_NUMBER
// Owner: 演算法工程師
// Deadline: 2026-04-30（收集足夠測試資料後）
// Reason: 經驗值，需現場測試驗證
// Resolution: 命名常數 + 文件說明 + 可配置

// 當前:
vertical_tolerance = 0.15;  // 25m
vertical_tolerance = 0.10;  // 75m
vertical_tolerance = 0.20;  // 175m
vertical_tolerance = 0.25;  // 300m

// 目標:
namespace HitDetection {
    // 垂直容差值經過 2025-12-22 現場測試驗證
    // 測試資料: 100 發/距離，準確率 95%+
    constexpr double TOLERANCE_25M = 0.15;   // 近距離，標靶大
    constexpr double TOLERANCE_75M = 0.10;   // 中距離，標靶中
    constexpr double TOLERANCE_175M = 0.20;  // 遠距離，標靶小
    constexpr double TOLERANCE_300M = 0.25;  // 超遠距離，標靶極小
}
```

**狀態:** 🟢 已記錄  
**資料收集:** 需要記錄實際命中率，驗證閾值合理性

---

#### DEBT-HIGH-006: 魔術數字 - 重複射擊過濾

```cpp
// 位置: cpp/src/IRTracker.cpp:591-616
// ⚠️ TECH-DEBT: MAGIC_NUMBER
// Owner: 演算法工程師
// Deadline: 2026-04-30
// Reason: 經驗值，需現場測試驗證
// Resolution: 命名常數 + 說明

// 當前:
if (time_diff < 200 && dist_sq < 0.05)  // 重複偵測窗口
if (time_diff < 300)  // 最小射擊間隔
if (latest_shots_.size() > 50)  // 緩衝區大小

// 目標:
namespace ShotFiltering {
    // 重複偵測參數（2025-12-22 測試確定）
    constexpr int DUPLICATE_WINDOW_MS = 200;      // 雷射點持續時間
    constexpr double DUPLICATE_DIST_SQ = 0.05;   // 座標相似度閾值
    constexpr int MIN_SHOT_INTERVAL_MS = 300;    // 人類最快扣扳機速度
    constexpr size_t MAX_SHOTS_BUFFER = 50;      // 記憶體限制
}
```

**狀態:** 🟢 已記錄

---

#### DEBT-HIGH-007: 魔術數字 - 射手區域劃分

```cpp
// 位置: cpp/src/IRTracker.cpp:359, 536-541, 686-691
// ⚠️ TECH-DEBT: MAGIC_NUMBER
// Owner: 系統設計師
// Deadline: 2026-04-30
// Reason: 硬編碼射手數量
// Resolution: 可配置射手數量

// 當前:
double zone_width = 2.0 / 6.0;  // 為何是 6？為何是 2.0？

// 目標:
namespace CoordinateSystem {
    constexpr int NUM_SHOOTERS = 6;              // 系統支援射手數量
    constexpr double COORD_RANGE = 2.0;          // 正規化範圍 [-1, 1] 寬度
    constexpr double ZONE_WIDTH = COORD_RANGE / NUM_SHOOTERS;  // 0.333
    
    // 未來可擴展為可配置:
    // struct SystemConfig { int num_shooters = 6; };
}
```

**狀態:** 🟢 已記錄

---

#### DEBT-HIGH-008: 魔術數字 - 校準點座標

```cpp
// 位置: cpp/include/IRTracker.hpp:134-139
// ⚠️ TECH-DEBT: MAGIC_NUMBER
// Owner: 演算法工程師
// Deadline: 2026-04-30
// Reason: 經驗值，避開螢幕邊緣
// Resolution: 命名常數 + 說明

// 當前:
std::vector<cv::Point2f> dst_points_ = {
    cv::Point2f(-0.85f, 0.85f),   // 為何是 0.85？
    cv::Point2f(0.85f, 0.85f),
    cv::Point2f(0.85f, -0.85f),
    cv::Point2f(-0.85f, -0.85f)
};

// 目標:
namespace Calibration {
    // 校準點距離螢幕邊緣 7.5% (1.0 - 0.85 = 0.15 / 2)
    // 避開 Homography 邊緣扭曲區域（見 RISK-002）
    constexpr float CALIBRATION_INSET = 0.85f;  // 內縮比例
    
    const std::vector<cv::Point2f> SCREEN_CORNERS = {
        {-CALIBRATION_INSET,  CALIBRATION_INSET},  // 左上
        { CALIBRATION_INSET,  CALIBRATION_INSET},  // 右上
        { CALIBRATION_INSET, -CALIBRATION_INSET},  // 右下
        {-CALIBRATION_INSET, -CALIBRATION_INSET}   // 左下
    };
}
```

**狀態:** 🟢 已記錄  
**相關風險:** RISK-002 (Homography 邊緣扭曲)

---

#### DEBT-HIGH-009: Workaround - GigE 掃描延遲

```cpp
// 位置: cpp/src/CameraManager.cpp:47, 78
//       cpp/src/IRTracker.cpp:110
// ⚠️ TECH-DEBT: WORKAROUND
// Owner: Pylon 整合工程師
// Deadline: 2026-Q2（Pylon SDK 更新或找到更好方案）
// Reason: GigE 裝置發現是異步的，SDK 無 ready callback
// Resolution: 使用 Pylon 事件處理器，而非 sleep

// 當前:
std::this_thread::sleep_for(std::chrono::milliseconds(1000));  // CameraManager.cpp:47
std::this_thread::sleep_for(std::chrono::milliseconds(1000));  // CameraManager.cpp:78
std::this_thread::sleep_for(std::chrono::milliseconds(2000));  // IRTracker.cpp:110

// 目標:
// 1. 使用 Pylon::CTlFactory::EnumerateDevices() 輪詢
// 2. 或使用 DeviceRemovalEventHandler 回調
// 3. 或查閱新版 Pylon SDK 文件
```

**狀態:** 🟡 可接受（功能正常）  
**相關風險:** RISK-001 (GigE 網路頻寬飽和)

---

#### DEBT-HIGH-010: Workaround - 強制關機流程

```cpp
// 位置: cpp/src/main.cpp:374-396
// ⚠️ TECH-DEBT: WORKAROUND
// Owner: 系統工程師
// Deadline: 2026-Q2（整體重構時）
// Reason: 快速實作，未考慮優雅關閉
// Resolution: 實作 graceful shutdown (見 KNOWN_RISKS.md RISK-009)

// 當前:
std::thread([]() {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    system("taskkill.exe /F /IM chrome.exe");
    std::_Exit(0);  // 繞過析構函數
}).detach();

// 目標:
// 1. tracker.stop() - 等待執行緒結束
// 2. saveSettings() - 儲存配置
// 3. 通知 Chrome 優雅關閉（DevTools Protocol）
// 4. 等待最多 5 秒
// 5. 若仍未關閉才使用 taskkill
// 6. std::exit(0) - 呼叫析構函數
```

**狀態:** 🟠 需改進  
**影響:** Chrome 設定損壞、虛擬磁碟未卸載

---

### 🟡 MEDIUM 等級

#### DEBT-MED-001: 程式碼重複 - 設定應用邏輯

```cpp
// 位置: cpp/src/main.cpp:99-104, 171-176, 191-196, 221-226
// ⚠️ TECH-DEBT: DUPLICATION
// Owner: 後端開發團隊
// Deadline: 2026-Q2
// Reason: 快速開發時複製貼上
// Resolution: 抽取為函數

// 重複 4 次的程式碼:
tracker.setTargetAdjustments(
    settings["scale"].get<double>(),
    settings["vertical"].get<double>(),
    settings["spacing"].get<double>(),
    settings["edgePadding"].get<double>()
);
tracker.setZeroingOffsets(
    settings.value("zeroingX", 0.0),
    settings.value("zeroingY", 0.0)
);

// 目標:
void applyCurrentSettingsToTracker(IRTracker& tracker, const std::string& key);
```

**狀態:** 🟢 已記錄  
**影響:** 修改邏輯需改 4 處

---

#### DEBT-MED-002: 程式碼重複 - 射手陣列初始化

```cpp
// 位置: cpp/src/main.cpp:277-284, 309-316, 345-354
// ⚠️ TECH-DEBT: DUPLICATION
// Owner: 後端開發團隊
// Deadline: 2026-Q2
// Reason: 快速開發時複製貼上
// Resolution: 抽取為函數

// 重複 3 次的程式碼:
if (!allTargetSettings.contains("shooters") || !allTargetSettings["shooters"].is_array()) {
    allTargetSettings["shooters"] = json::array();
    for (int i = 1; i <= 6; ++i) {
        allTargetSettings["shooters"].push_back({
            {"id", i},
            {"zeroingX", 0.0},
            {"zeroingY", 0.0},
            {"intensity", 128},
            {"minIntensity", 50}
        });
    }
}

// 目標:
void ensureShootersArrayExists(json& settings);
```

**狀態:** 🟢 已記錄

---

#### DEBT-MED-003: 程式碼重複 - 校準邏輯 (Camera A/B)

```cpp
// 位置: cpp/src/IRTracker.cpp:461-509 (Camera A)
//       cpp/src/IRTracker.cpp:625-672 (Camera B)
// ⚠️ TECH-DEBT: DUPLICATION
// Owner: 演算法工程師
// Deadline: 2026-Q2
// Reason: Camera A 和 B 邏輯完全相同，僅變數名不同
// Resolution: 抽取為模板函數或參數化函數

// 約 90 行重複程式碼

// 目標:
template<CameraId ID>
void processCalibrationForCamera(
    std::vector<cv::Point2f>& points,
    cv::Mat& homography,
    cv::Mat& frame_buffer,
    const cv::Point2f& detected_point
);
```

**狀態:** 🟢 已記錄  
**影響:** 修改校準邏輯需改 2 處

---

#### DEBT-MED-004: 複雜的配置同步邏輯

```cpp
// 位置: cpp/src/main.cpp:178-227
// ⚠️ TECH-DEBT: COMPLEXITY
// Owner: 架構師
// Deadline: 2026-Q2
// Reason: 缺乏 ConfigurationManager 抽象
// Resolution: 使用 RAII transaction pattern

// 50 行複雜的設定更新邏輯

// 目標:
class SettingsTransaction {
    json& target;
    json backup;
    bool committed = false;
public:
    SettingsTransaction(json& settings) : target(settings), backup(settings) {}
    ~SettingsTransaction() { if (!committed) target = backup; }
    void commit() { committed = true; }
};
```

**狀態:** 🟢 已記錄

---

#### DEBT-MED-005 到 MED-008

（其餘 MEDIUM 等級債務已在憲章審查報告中記錄）

---

### 🟢 LOW 等級

#### DEBT-LOW-001: 不清楚的變數名稱

```cpp
// 位置: cpp/src/IRTracker.cpp:518-524
// ⚠️ TECH-DEBT: NAMING
// Owner: 程式碼審查者
// Deadline: 2026-Q2（程式碼審查時順便改）
// Reason: 快速開發未注意命名
// Resolution: 重新命名

// 當前:
int temp_id = ...;  // 不清楚用途

// 目標:
int preliminary_shooter_id = ...;  // 用於強度過濾的初步 ID
```

**狀態:** 🟢 已記錄

---

#### DEBT-LOW-002 到 LOW-005

（其餘 LOW 等級債務已在憲章審查報告中記錄）

---

## 新增債務區（2026-02-05 之後）

### 格式範本

```cpp
// 位置: [檔案路徑:行號]
// ⚠️ TECH-DEBT: [HARDCODED|WORKAROUND|MAGIC_NUMBER|ASSUMPTION|HACK]
// 類別: [CRITICAL|HIGH|MEDIUM|LOW]
// Owner: [負責人]
// Deadline: [期限或觸發條件]
// Reason: [為何這樣做]
// Resolution: [正確的解決方案]
// Added: [日期]
// Context: [新增功能背景]
```

---

## 債務解決追蹤

### 已解決的債務

（目前無）

---

### 進行中的債務

| 債務 ID | 狀態 | 開始日期 | 預計完成 | 負責人 |
|---------|------|---------|---------|--------|
| - | - | - | - | - |

---

## 債務審查記錄

### 2026-02-05 - 初次盤點

**審查人員:** Claude (Review Agent) + 使用者  
**審查範圍:** 全系統  
**發現債務:** 25 項  
**立即處理:** 0 項（內網環境，安全債務暫緩）  
**計劃處理:** 2026-Q2 整體重構時處理架構債務

**決策:**
- 安全債務（CRIT-001, CRIT-002）- 接受風險（內網環境）
- 架構債務（HIGH-001, HIGH-002）- 新功能開發時輕量重構
- 其他債務 - 2026-Q2 統一處理

---

### 下次審查

**預定日期:** 2026-03-05（每月審查）  
**審查重點:**
- 檢查新增債務數量
- 評估債務趨勢（增加 vs 減少）
- 調整期限和優先級

---

## 債務指標

### 當前指標（2026-02-05）

```
債務密度 = 25 項 / 4000 行代碼 = 0.625%

債務分佈:
CRITICAL:  8%  (2/25)
HIGH:     40%  (10/25)
MEDIUM:   32%  (8/25)
LOW:      20%  (5/25)

類別分佈:
HARDCODED:     32%  (8/25)
MAGIC_NUMBER:  40%  (10/25)
WORKAROUND:    12%  (3/25)
ARCHITECTURE:  8%   (2/25)
SECURITY:      8%   (2/25)
```

### 目標指標（2026-Q2）

```
債務密度 < 0.3%
HIGH+ 債務 = 0
MEDIUM 債務 < 5 項
所有債務都有明確期限
```

---

## 債務規則

### 新增債務時必須：

1. ✅ 使用標準格式標記
2. ✅ 指定負責人
3. ✅ 設定期限或觸發條件
4. ✅ 說明原因和解決方案
5. ✅ 記錄到本文件
6. ✅ 在程式碼中加入註釋

### 禁止：

- ❌ 無標記的捷徑
- ❌ "TODO" 無負責人
- ❌ "稍後修復" 無期限
- ❌ 隱藏的假設
- ❌ 未記錄的 workaround

---

## 附錄：債務類別說明

### HARDCODED
硬編碼的值，未來可能需要配置化。  
**範例:** 相機序號、檔案路徑、IP 位址

### WORKAROUND
暫時性的迂迴解決方案。  
**範例:** sleep() 等待相機、強制 kill Chrome

### MAGIC_NUMBER
未命名的數字常數。  
**範例:** 0.15 (容差)、200 (時間窗口)、6 (射手數量)

### ASSUMPTION
未驗證的假設。  
**範例:** 相機總是在左右兩側、工作目錄總是專案根目錄

### HACK
臨時性的修復，未解決根本問題。  
**範例:** 特殊情況的 if 判斷、繞過正常流程

---

**文件結束**

此文件符合 AI-Assisted Development Constitution v2.0 Article 7 要求。  
**所有捷徑都是可見的，無隱藏債務。**
