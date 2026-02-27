# T91 紅外線射擊追蹤系統 - 模組邊界定義

**版本:** 1.0  
**日期:** 2026-02-05  
**狀態:** 初始版本  
**依據:** AI-Assisted Development Constitution v2.0 Article 3

---

## 文件目的

本文件定義 T91 系統中每個模組的**單一職責**、**公開介面**、**不負責的事項**以及**模組間的依賴關係**。

根據憲章 Article 3：

> "A valid module MUST:
> 1. Own exactly ONE core responsibility
> 2. Be the single source of truth for that responsibility  
> 3. Be independently assignable to one AI Agent for development
> 4. Interact with other modules ONLY through explicit interfaces"

本文件同時涵蓋：
- **當前架構** (現狀分析與問題)
- **目標架構** (重構後的理想狀態)

---

## 模組概覽

### 當前架構（存在違規）

```
T91 Shooting Tracker
├── main.cpp (438 行) ⚠️ 4 個職責
│   ├── HTTP API 伺服器
│   ├── 配置管理
│   ├── 應用程式生命週期
│   └── 業務邏輯
│
├── IRTracker (1000+ 行) ⚠️ 6 個職責
│   ├── 影像處理協調
│   ├── 校準邏輯
│   ├── 命中偵測與計分
│   ├── 配置管理
│   ├── 狀態管理
│   └── 檔案 I/O
│
├── CameraManager ✅ 職責單一
│   └── 相機硬體抽象
│
└── Detector ⚠️ 輕微混合
    ├── IR 點偵測
    └── 診斷日誌
```

### 目標架構（重構後）

```
T91 Shooting Tracker
├── main (~50 行) ✅ 單一職責
│   └── 應用程式入口與組裝
│
├── ConfigurationManager ✅ 單一職責
│   └── 配置管理與持久化
│
├── HttpApiServer ✅ 單一職責
│   └── HTTP 請求路由
│
├── SystemController ✅ 單一職責
│   └── 生命週期管理
│
├── IRTracker (簡化) ✅ 單一職責
│   └── 高階影像處理協調
│
├── CalibrationService ✅ 單一職責
│   └── 相機校準邏輯
│
├── HitDetectionService ✅ 單一職責
│   └── 命中判定與座標轉換
│
├── TrackerConfiguration ✅ 單一職責
│   └── 追蹤器配置參數
│
├── TrackerState ✅ 單一職責
│   └── 執行緒安全狀態管理
│
├── ResultsArchiver ✅ 單一職責
│   └── 結果匯出
│
├── CameraManager ✅ 單一職責 (已存在)
│   └── 相機硬體抽象
│
└── Detector ✅ 單一職責 (輕微調整)
    └── IR 點偵測演算法
```

---

## 第一部分：當前架構分析

## 模組 1: main.cpp ⚠️ **違反單一職責原則**

### 檔案資訊

- **位置:** `cpp/src/main.cpp`
- **大小:** 438 行
- **憲章狀態:** ❌ CRITICAL-002 違反

### 當前職責 (4 個，應該只有 1 個)

#### 職責 1: HTTP API 伺服器 (131-427 行)

**功能:**
- 定義 20+ 個 HTTP 端點
- 解析 JSON 請求
- 格式化 JSON 回應
- 設定 CORS headers
- 啟動 HTTP 伺服器監聽

**程式碼範例:**
```cpp
svr.Post("/command", [&](const httplib::Request &req, httplib::Response &res) {
    // ... 解析 action
    // ... 執行對應邏輯
    // ... 回傳 JSON
});
```

#### 職責 2: 配置管理 (16-81 行)

**功能:**
- 定義全域變數 (`allTargetSettings`, `currentWeather`, `currentTime`, `currentDistance`)
- 載入 `target_settings.json`
- 儲存設定到檔案
- 同步設定到 IRTracker

**程式碼範例:**
```cpp
json allTargetSettings = {};
std::string currentWeather = "sunny";
// ...
std::ifstream ifs("target_settings.json");
ifs >> allTargetSettings;
```

#### 職責 3: 應用程式生命週期 (83-437 行)

**功能:**
- 初始化 IRTracker
- 啟動 HTTP 伺服器
- 處理系統關機
- 啟動 Chrome 瀏覽器

**程式碼範例:**
```cpp
IRTracker tracker;
tracker.start();
system("start chrome...");
svr.listen("0.0.0.0", 8081);
```

#### 職責 4: 業務邏輯 (27-80, 178-373 行)

**功能:**
- 計算目標調整參數
- 驗證射手參數
- 範圍檢查（部分）
- 設定值轉換

**程式碼範例:**
```cpp
if (data.contains("scale"))
    allTargetSettings[key]["scale"] = data["scale"];
// ... 重複多次
tracker.setTargetAdjustments(...);
```

### 問題分析

| 問題 | 嚴重性 | 說明 |
|------|--------|------|
| 違反單一職責 | CRITICAL | 4 個修改理由 → 高耦合 |
| 程式碼重複 | HIGH | 設定應用邏輯出現 4 次 (99-104, 171-176, 191-196, 221-226) |
| 難以測試 | HIGH | 無法獨立測試 HTTP 路由或配置邏輯 |
| 新人難理解 | MEDIUM | 438 行混合多種關注點 |

### 不負責事項（應該也沒在做）

- ❌ 相機硬體控制（由 IRTracker 負責）
- ❌ 影像處理邏輯（由 IRTracker 負責）
- ❌ 前端 UI 渲染（由 Chrome 負責）

---

## 模組 2: IRTracker ⚠️ **違反單一職責原則**

### 檔案資訊

- **位置:** `cpp/include/IRTracker.hpp` + `cpp/src/IRTracker.cpp`
- **大小:** ~1000+ 行
- **憲章狀態:** ❌ HIGH-001 違反

### 當前職責 (6 個，應該只有 1 個)

#### 職責 1: 影像處理協調 (454-770 行)

**功能:**
- 運行 `processingLoop()` 執行緒
- 從雙相機抓取影像
- 呼叫 Detector 偵測 IR 點
- 管理處理執行緒的啟動/停止

**關鍵方法:**
- `start()` - 啟動背景執行緒
- `stop()` - 停止執行緒
- `processingLoop()` - 主迴圈

#### 職責 2: 校準邏輯 (169-178, 461-509, 625-672 行)

**功能:**
- 收集 4 個校準點
- 呼叫 `cv::findHomography()` 計算變換矩陣
- 儲存/載入 `calibration_a.json` 和 `calibration_b.json`

**關鍵方法:**
- `startCalibrationA()` / `startCalibrationB()`
- `loadCalibration()` / `saveCalibration()`

**程式碼重複:**
- Camera A 和 B 的校準邏輯幾乎完全相同（~90 行重複）

#### 職責 3: 命中偵測與計分 (352-387, 440-452 行)

**功能:**
- 座標轉換（相機像素 → 正規化螢幕空間）
- 計算射手 ID (X 座標 → [1-6])
- 檢查垂直容差（依距離模式）
- 更新命中計數器

**關鍵方法:**
- `registerHit(shooter_id)`
- `checkHit()` (內部邏輯)

#### 職責 4: 配置管理 (54-76, 326-349 行)

**功能:**
- 20+ 個 setter 方法
- 管理目標調整參數
- 管理歸零偏移（全域、模式、射手）
- 管理射手強度設定

**關鍵方法:**
- `setTargetAdjustments(scale, vertical, spacing, edgePadding)`
- `setZeroingOffsets(x, y)`
- `setShooterIntensity()` / `setShooterZeroing()` ...

**問題:** 介面過於龐大，違反介面隔離原則

#### 職責 5: 狀態管理 (275-324 行)

**功能:**
- 管理 `latest_shots_` 向量（最近 50 筆）
- 管理 `hits_[6]` 和 `targets_down_[6]` 陣列
- 三個 mutex (shots_mutex_, calib_mutex_, status_mutex_)
- 校準模式切換

**關鍵方法:**
- `getLatestShots()`
- `knockdownTarget()` / `resetTarget()`
- `getHitCount()` / `isTargetDown()`

#### 職責 6: 檔案 I/O (180-254 行)

**功能:**
- 匯出射擊結果到 CSV (`../射擊成績/成績_*.csv`)
- 儲存 JSON 結果 (`last_results.json`)
- 寫入除錯日誌 (`t91_debug.log`)

**關鍵方法:**
- `saveResults(shooter_id)`

**程式碼:**
```cpp
std::ofstream csv_file(csv_filename, std::ios::app);
csv_file << /* CSV 欄位 */ << std::endl;

std::ofstream json_file("last_results.json");
json_file << j.dump(2);
```

### 問題分析

| 問題 | 嚴重性 | 說明 |
|------|--------|------|
| 違反單一職責 | HIGH | 6 個修改理由 → 類別過於龐大 |
| 程式碼重複 | MEDIUM | 校準邏輯 Camera A/B 重複 ~90 行 |
| 介面污染 | MEDIUM | 20+ 個 setter 方法 |
| 難以測試 | HIGH | 無法 mock 個別職責 |
| 違反開放封閉 | MEDIUM | 新增功能需修改核心類別 |

### 不負責事項（應該也沒在做）

- ❌ HTTP 請求處理（由 main.cpp 負責）
- ❌ UI 渲染（由前端負責）
- ❌ 硬體驅動（由 Pylon SDK 負責）

---

## 模組 3: CameraManager ✅ **符合單一職責**

### 檔案資訊

- **位置:** `cpp/include/CameraManager.hpp` + `cpp/src/CameraManager.cpp`
- **大小:** ~200 行
- **憲章狀態:** ✅ 符合 Article 3

### 單一職責

**職責:** 相機硬體抽象層

### 負責事項

1. **定義 ICamera 介面**
   - `open()` - 開啟相機
   - `grab(cv::Mat& frame)` - 抓取影像幀
   - `close()` - 關閉相機

2. **實作 BaslerCamera**
   - 使用 Pylon SDK 控制 Basler GigE 相機
   - 設定相機參數（幀率、曝光）
   - 錯誤處理與重試邏輯

3. **實作 MockCamera**
   - 測試用假相機
   - 從 OpenCV VideoCapture 讀取
   - 或產生空白影像

### 公開介面

```cpp
class ICamera {
public:
    virtual bool open() = 0;
    virtual bool grab(cv::Mat& frame) = 0;
    virtual void close() = 0;
    virtual ~ICamera() = default;
};

class BaslerCamera : public ICamera {
public:
    BaslerCamera(int64_t serial_number);
    bool open() override;
    bool grab(cv::Mat& frame) override;
    void close() override;
};
```

### 不負責事項

- ❌ 影像處理（由 IRTracker/Detector 負責）
- ❌ 校準邏輯（由 IRTracker 負責，應拆出）
- ❌ 命中判定（由 IRTracker 負責，應拆出）

### 依賴關係

**依賴（輸入）:**
- Basler Pylon SDK (外部)
- OpenCV (cv::Mat)

**被依賴（輸出）:**
- IRTracker 使用 ICamera 介面

### 評價

✅ **優秀範例** - 職責清晰、介面乾淨、易於測試

---

## 模組 4: Detector ⚠️ **輕微混合職責**

### 檔案資訊

- **位置:** `cpp/include/Detector.hpp`
- **大小:** 72 行
- **憲章狀態:** ⚠️ MEDIUM-001 輕微違反

### 主要職責

**職責:** IR 點偵測演算法

### 負責事項

1. **偵測 IR 雷射點**
   - 閾值過濾（threshold）
   - 形態學操作（erode/dilate）
   - 輪廓偵測（findContours）
   - 面積過濾（minArea, maxArea）

2. **回傳偵測結果**
   - `std::optional<cv::Point2f>` - 偵測到的點或無

3. **診斷日誌** ⚠️ 次要職責（應分離）
   - 統計偵測到的輪廓數量
   - 呼叫外部日誌回調

### 公開介面

```cpp
class Detector {
public:
    std::optional<cv::Point2f> detect(const cv::Mat& frame);
    void setDiagnosticsCallback(std::function<void(int, int, int)> cb);
    void updateThreshold(int threshold);
};
```

### 問題分析

| 問題 | 嚴重性 | 說明 |
|------|--------|------|
| 混合日誌關注點 | LOW | 診斷日誌應由呼叫者處理 |
| 副作用 | LOW | `detect()` 呼叫日誌回調（非純函數） |

### 建議改進

分離診斷資訊為回傳值：

```cpp
struct DetectionResult {
    std::optional<cv::Point2f> point;
    int contours_found;
    int contours_after_area_filter;
    int final_detections;
};

DetectionResult detect(const cv::Mat& frame);
```

### 不負責事項

- ❌ 相機影像抓取（由 CameraManager 負責）
- ❌ 座標轉換（由 IRTracker 負責，應拆出）
- ❌ 命中判定（由 IRTracker 負責，應拆出）

---

## 第二部分：目標架構定義

以下定義重構後的理想模組結構。

---

## 目標模組 1: main

### 檔案資訊

- **位置:** `cpp/src/main.cpp` (重構後)
- **預期大小:** ~50 行
- **狀態:** 🎯 目標架構

### 單一職責

**職責:** 應用程式入口點與依賴注入組裝

### 負責事項

1. **初始化所有模組**
   - 建構 ConfigurationManager
   - 建構 IRTracker 及其服務
   - 建構 HttpApiServer
   - 建構 SystemController

2. **組裝依賴關係**
   - 注入 ConfigurationManager 到 HttpApiServer
   - 注入 Tracker 到 SystemController

3. **啟動應用程式**
   - 呼叫 SystemController::initialize()
   - 阻塞於 HTTP 伺服器監聽

### 程式碼骨架

```cpp
int main() {
    try {
        // 建構模組
        ConfigurationManager config_mgr;
        config_mgr.loadSettings();
        
        IRTracker tracker;
        
        HttpApiServer http_server(config_mgr, tracker);
        SystemController controller(tracker, http_server);
        
        // 啟動
        controller.initialize();
        http_server.start();  // 阻塞
        
        // 關閉
        controller.shutdown();
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
```

### 不負責事項

- ❌ HTTP 路由邏輯（由 HttpApiServer 負責）
- ❌ 配置管理（由 ConfigurationManager 負責）
- ❌ 生命週期細節（由 SystemController 負責）

### 依賴關係

**依賴（建構）:** 所有模組  
**被依賴:** 無（頂層）

---

## 目標模組 2: ConfigurationManager

### 檔案資訊

- **位置:** `cpp/include/ConfigurationManager.hpp` + `cpp/src/ConfigurationManager.cpp`
- **預期大小:** ~200 行
- **狀態:** 🎯 目標架構

### 單一職責

**職責:** 管理所有配置資料的載入、儲存、查詢與更新

### 負責事項

1. **載入與儲存設定**
   - 讀取 `target_settings.json`
   - 寫入 `target_settings.json`
   - 錯誤處理與預設值

2. **配置查詢**
   - 取得目標設定（sunny/rainy/day/night/25m/...）
   - 取得射手配置（歸零、強度）
   - 取得環境變數（天氣、時間、距離）

3. **配置更新**
   - 更新目標參數（scale, vertical, spacing, edgePadding）
   - 更新射手歸零/強度
   - 驗證輸入範圍

4. **同步到追蹤器**
   - 將當前配置應用到 IRTracker
   - 呼叫對應的 setter 方法

### 公開介面

```cpp
class ConfigurationManager {
public:
    // 載入與儲存
    bool loadSettings();
    bool saveSettings();
    
    // 查詢
    TargetSettings getTargetSettings(const std::string& key);
    ShooterConfig getShooterConfig(int shooter_id);
    std::string getCurrentWeather();
    std::string getCurrentTime();
    std::string getCurrentDistance();
    
    // 更新
    void updateTargetSettings(const std::string& key, const TargetSettings& settings);
    void updateShooterConfig(int shooter_id, const std::string& field, const json& value);
    void setEnvironment(const std::string& weather, const std::string& time, const std::string& distance);
    
    // 同步到追蹤器
    void applyToTracker(IRTracker& tracker);
    
private:
    json all_target_settings_;
    std::string current_weather_;
    std::string current_time_;
    std::string current_distance_;
};
```

### 不負責事項

- ❌ HTTP 請求處理（由 HttpApiServer 負責）
- ❌ 實際追蹤邏輯（由 IRTracker 負責）
- ❌ UI 渲染（由前端負責）

### 依賴關係

**依賴（輸入）:**
- nlohmann/json (解析)
- std::filesystem (檔案 I/O)

**被依賴（輸出）:**
- HttpApiServer (查詢/更新配置)
- main (初始化)

---

## 目標模組 3: HttpApiServer

### 檔案資訊

- **位置:** `cpp/include/HttpApiServer.hpp` + `cpp/src/HttpApiServer.cpp`
- **預期大小:** ~400 行
- **狀態:** 🎯 目標架構

### 單一職責

**職責:** HTTP 請求路由與 JSON 序列化/反序列化

### 負責事項

1. **註冊所有端點**
   - `/command` POST 端點
   - `/shots` GET 端點
   - `/state` GET 端點

2. **請求解析**
   - 解析 JSON body
   - 提取 `action` 欄位
   - 驗證必要參數存在

3. **路由到處理器**
   - 根據 `action` 分派到對應函數
   - 呼叫 ConfigurationManager 或 IRTracker 方法

4. **回應格式化**
   - 組裝 JSON 回應
   - 設定 HTTP status code
   - 設定 CORS headers

5. **輸入驗證**
   - 範圍檢查（shooter_id, scale, ...）
   - 型別驗證
   - 回傳錯誤訊息

### 公開介面

```cpp
class HttpApiServer {
public:
    HttpApiServer(ConfigurationManager& config, IRTracker& tracker);
    
    void start(const std::string& host, int port);
    void stop();
    
private:
    void registerEndpoints();
    
    // 端點處理器
    void handleCommand(const httplib::Request& req, httplib::Response& res);
    void handleGetShots(const httplib::Request& req, httplib::Response& res);
    void handleGetState(const httplib::Request& req, httplib::Response& res);
    
    // Action 處理器
    void handle_adjust_targets(const json& data);
    void handle_knockdown_target(const json& data);
    void handle_register_hit(const json& data);
    // ... 20+ 個 action 處理器
    
    ConfigurationManager& config_;
    IRTracker& tracker_;
    httplib::Server svr_;
};
```

### 不負責事項

- ❌ 配置資料管理（由 ConfigurationManager 負責）
- ❌ 追蹤邏輯（由 IRTracker 負責）
- ❌ 系統關機邏輯（由 SystemController 負責）

### 依賴關係

**依賴（輸入）:**
- cpp-httplib (HTTP 伺服器)
- nlohmann/json (JSON 解析)
- ConfigurationManager (配置操作)
- IRTracker (追蹤器操作)

**被依賴（輸出）:**
- main (啟動伺服器)

---

## 目標模組 4: SystemController

### 檔案資訊

- **位置:** `cpp/include/SystemController.hpp` + `cpp/src/SystemController.cpp`
- **預期大小:** ~150 行
- **狀態:** 🎯 目標架構

### 單一職責

**職責:** 應用程式生命週期管理

### 負責事項

1. **系統初始化**
   - 啟動 IRTracker
   - 啟動 Chrome UI (可選)
   - 檢查前置條件（相機、檔案）

2. **系統關機**
   - 停止 IRTracker
   - 停止 HttpApiServer
   - 關閉 Chrome (graceful)
   - 清理資源

3. **UI 啟動**
   - 組裝 Chrome 啟動命令
   - 執行 `system()` 或更安全的方式
   - 錯誤處理

### 公開介面

```cpp
class SystemController {
public:
    SystemController(IRTracker& tracker, HttpApiServer& http_server);
    
    bool initialize();
    void shutdown();
    void launchUI();
    
private:
    void gracefulShutdownChrome();
    bool checkPrerequisites();
    
    IRTracker& tracker_;
    HttpApiServer& http_server_;
    bool ui_launched_;
};
```

### 不負責事項

- ❌ HTTP 請求處理（由 HttpApiServer 負責）
- ❌ 追蹤邏輯（由 IRTracker 負責）
- ❌ 配置管理（由 ConfigurationManager 負責）

### 依賴關係

**依賴（輸入）:**
- IRTracker (啟動/停止)
- HttpApiServer (停止)

**被依賴（輸出）:**
- main (初始化/關機)
- HttpApiServer (shutdown action 呼叫)

---

## 目標模組 5: IRTracker (簡化版)

### 檔案資訊

- **位置:** `cpp/include/IRTracker.hpp` + `cpp/src/IRTracker.cpp`
- **預期大小:** ~200 行（從 1000+ 縮減）
- **狀態:** 🎯 目標架構

### 單一職責

**職責:** 高階影像處理協調器

### 負責事項

1. **執行緒管理**
   - 啟動/停止 `processingLoop()` 執行緒
   - 管理 `running_` 標誌

2. **影像抓取協調**
   - 從 CameraManager 抓取影像
   - 呼叫 Detector 偵測 IR 點

3. **服務協調**
   - 若在校準模式，呼叫 CalibrationService
   - 若在追蹤模式，呼叫 HitDetectionService
   - 更新 TrackerState

4. **配置介面**
   - 接收來自 ConfigurationManager 的設定
   - 傳遞到各服務

### 公開介面（簡化）

```cpp
class IRTracker {
public:
    IRTracker();
    ~IRTracker();
    
    // 生命週期
    void start();
    void stop();
    
    // 配置（委派給服務）
    void applyConfiguration(const TrackerConfiguration& config);
    
    // 狀態查詢（委派給 TrackerState）
    std::vector<ShotResult> getLatestShots();
    int getHitCount(int shooter_id);
    bool isTargetDown(int shooter_id);
    
    // 命令（委派給 TrackerState）
    void knockdownTarget(int shooter_id);
    void resetTarget(int shooter_id);
    void registerHit(int shooter_id);
    
    // 校準（委派給 CalibrationService）
    void startCalibrationA();
    void startCalibrationB();
    
private:
    void processingLoop();
    
    std::unique_ptr<CameraManager> camera_mgr_;
    std::unique_ptr<CalibrationService> calibration_svc_;
    std::unique_ptr<HitDetectionService> hit_detection_svc_;
    std::unique_ptr<TrackerState> state_;
    std::unique_ptr<ResultsArchiver> archiver_;
    
    Detector detector_;
    std::thread processing_thread_;
    std::atomic<bool> running_;
};
```

### 不負責事項（已委派）

- ❌ 校準數學計算（由 CalibrationService 負責）
- ❌ 命中判定邏輯（由 HitDetectionService 負責）
- ❌ 狀態儲存（由 TrackerState 負責）
- ❌ 檔案 I/O（由 ResultsArchiver 負責）
- ❌ 配置參數（由 TrackerConfiguration 負責）

### 依賴關係

**依賴（輸入）:**
- CameraManager (影像抓取)
- Detector (IR 點偵測)
- CalibrationService (校準)
- HitDetectionService (命中判定)
- TrackerState (狀態管理)
- ResultsArchiver (結果匯出)

**被依賴（輸出）:**
- HttpApiServer (狀態查詢、命令)
- main (初始化)

---

## 目標模組 6: CalibrationService

### 檔案資訊

- **位置:** `cpp/include/CalibrationService.hpp` + `cpp/src/CalibrationService.cpp`
- **預期大小:** ~250 行
- **狀態:** 🎯 目標架構

### 單一職責

**職責:** 相機校準邏輯（4 點 homography）

### 負責事項

1. **收集校準點**
   - 儲存相機像素座標
   - 驗證點數量（必須 4 個）
   - 對應到螢幕角落座標

2. **計算 Homography**
   - 呼叫 `cv::findHomography()`
   - 驗證矩陣有效性
   - 快取結果

3. **持久化**
   - 儲存到 `calibration_a.json` / `calibration_b.json`
   - 載入已存在的校準資料
   - 錯誤處理

4. **查詢介面**
   - 回傳 Homography 矩陣
   - 檢查校準狀態（是否已校準）

### 公開介面

```cpp
enum class CameraId { A, B };

class CalibrationService {
public:
    // 校準流程
    void startCalibration(CameraId camera);
    bool addCalibrationPoint(CameraId camera, cv::Point2f point);
    bool computeHomography(CameraId camera);
    void cancelCalibration(CameraId camera);
    
    // 查詢
    cv::Mat getHomography(CameraId camera) const;
    bool isCalibrated(CameraId camera) const;
    CameraId getCurrentCalibrationCamera() const;
    
    // 持久化
    bool loadCalibration(const std::string& filepath_a, const std::string& filepath_b);
    bool saveCalibration(const std::string& filepath_a, const std::string& filepath_b);
    
private:
    struct CameraCalibration {
        std::vector<cv::Point2f> points;
        cv::Mat homography;
        bool valid = false;
    };
    
    CameraCalibration calib_a_;
    CameraCalibration calib_b_;
    std::optional<CameraId> active_calibration_;
    
    mutable std::mutex calib_mutex_;
};
```

### 不負責事項

- ❌ 影像抓取（由 CameraManager 負責）
- ❌ IR 點偵測（由 Detector 負責）
- ❌ 座標轉換應用（由 HitDetectionService 負責）

### 依賴關係

**依賴（輸入）:**
- OpenCV (cv::findHomography, cv::Mat)
- nlohmann/json (檔案 I/O)

**被依賴（輸出）:**
- IRTracker (校準操作)
- HitDetectionService (取得 homography)

---

## 目標模組 7: HitDetectionService

### 檔案資訊

- **位置:** `cpp/include/HitDetectionService.hpp` + `cpp/src/HitDetectionService.cpp`
- **預期大小:** ~300 行
- **狀態:** 🎯 目標架構

### 單一職責

**職責:** 座標轉換與命中判定邏輯

### 負責事項

1. **座標轉換**
   - 應用 Homography 矩陣
   - 應用歸零偏移（全域、模式、射手）
   - 回傳正規化座標 [-1, 1]

2. **射手 ID 計算**
   - 根據 X 座標計算射手區域 [1-6]
   - 處理邊界情況
   - 應用相機-射手對應 (A=1-3, B=4-6)

3. **命中判定**
   - 檢查垂直容差（依距離模式）
   - 計算目標中心點
   - 回傳是否命中

4. **重複過濾**
   - 檢查時間間隔 (< 200ms)
   - 檢查空間距離 (< 0.05 正規化距離)
   - 避免重複計數

5. **射手強度匹配** (可選)
   - 根據 IR 強度匹配射手
   - 輔助射手 ID 判定

### 公開介面

```cpp
class HitDetectionService {
public:
    HitDetectionService(CalibrationService& calibration);
    
    // 配置
    void setTargetAdjustments(const TargetAdjustments& adjustments);
    void setZeroingOffsets(const ZeroingOffsets& offsets);
    void setShooterIntensities(const std::array<ShooterIntensity, 6>& intensities);
    void setDistanceMode(const std::string& distance);
    
    // 座標轉換
    cv::Point2f transformCoordinate(cv::Point2f camera_point, 
                                    CameraId camera,
                                    int shooter_id);
    
    // 射手 ID 計算
    int calculateShooterId(float normalized_x, CameraId camera);
    
    // 命中判定
    bool isHit(cv::Point2f normalized_point, 
               int shooter_id);
    
    // 重複過濾
    bool isDuplicate(const ShotResult& shot, 
                     const std::vector<ShotResult>& recent_shots);
    
    // 強度匹配
    int matchShooterByIntensity(int preliminary_id, 
                                 int detected_intensity);
    
private:
    CalibrationService& calibration_;
    TargetAdjustments adjustments_;
    ZeroingOffsets global_zeroing_;
    ZeroingOffsets mode_zeroing_;
    std::array<cv::Point2f, 6> shooter_zeroing_;
    std::array<ShooterIntensity, 6> shooter_intensities_;
    std::string distance_mode_;
    
    float getVerticalTolerance() const;
    cv::Point2f calculateTargetCenter(int shooter_id) const;
};
```

### 不負責事項

- ❌ 校準計算（由 CalibrationService 負責）
- ❌ 狀態儲存（由 TrackerState 負責）
- ❌ 影像偵測（由 Detector 負責）

### 依賴關係

**依賴（輸入）:**
- CalibrationService (取得 homography)
- OpenCV (cv::perspectiveTransform, cv::Point2f)

**被依賴（輸出）:**
- IRTracker (命中判定)

---

## 目標模組 8: TrackerState

### 檔案資訊

- **位置:** `cpp/include/TrackerState.hpp` + `cpp/src/TrackerState.cpp`
- **預期大小:** ~200 行
- **狀態:** 🎯 目標架構

### 單一職責

**職責:** 執行緒安全的追蹤器狀態管理

### 負責事項

1. **射擊記錄管理**
   - 維護 `latest_shots_` 向量（最多 50 筆）
   - 執行緒安全的加入/查詢
   - 自動移除舊記錄

2. **命中計數管理**
   - 維護 `hits_[6]` 陣列
   - 執行緒安全的增加/重置/查詢

3. **標靶狀態管理**
   - 維護 `targets_down_[6]` 陣列
   - 執行緒安全的倒下/重置/查詢

4. **Mutex 保護**
   - 使用 `shots_mutex_` 保護射擊記錄
   - 使用 `status_mutex_` 保護命中與標靶狀態
   - 確保執行緒安全

### 公開介面

```cpp
class TrackerState {
public:
    // 射擊記錄
    void addShot(const ShotResult& shot);
    std::vector<ShotResult> getLatestShots();
    
    // 命中計數
    void recordHit(int shooter_id);
    int getHitCount(int shooter_id);
    std::array<int, 6> getAllHits();
    void resetAllHits();
    
    // 標靶狀態
    void knockdownTarget(int shooter_id);
    void resetTarget(int shooter_id);
    bool isTargetDown(int shooter_id);
    std::array<bool, 6> getAllTargetStates();
    
private:
    std::vector<ShotResult> latest_shots_;
    std::array<int, 6> hits_ = {0};
    std::array<bool, 6> targets_down_ = {false};
    
    mutable std::mutex shots_mutex_;
    mutable std::mutex status_mutex_;
};
```

### 不負責事項

- ❌ 命中判定邏輯（由 HitDetectionService 負責）
- ❌ 結果匯出（由 ResultsArchiver 負責）
- ❌ HTTP 回應（由 HttpApiServer 負責）

### 依賴關係

**依賴（輸入）:**
- 無（純狀態容器）

**被依賴（輸出）:**
- IRTracker (狀態操作)
- HttpApiServer (狀態查詢)
- ResultsArchiver (取得射擊記錄)

---

## 目標模組 9: ResultsArchiver

### 檔案資訊

- **位置:** `cpp/include/ResultsArchiver.hpp` + `cpp/src/ResultsArchiver.cpp`
- **預期大小:** ~150 行
- **狀態:** 🎯 目標架構

### 單一職責

**職責:** 射擊結果匯出（CSV 與 JSON）

### 負責事項

1. **CSV 匯出**
   - 格式化中文欄位
   - UTF-8 BOM 編碼
   - 附加到現有檔案
   - 檔名產生：`成績_YYYYMMDD_HHMMSS_射手X.csv`

2. **JSON 匯出**
   - 結構化資料格式
   - 寫入 `last_results.json`
   - 漂亮格式化 (indent=2)

3. **檔名產生**
   - 時間戳格式化
   - 射手 ID 嵌入
   - 路徑處理 (`../射擊成績/`)

4. **錯誤處理**
   - 檢查目錄存在
   - 檔案寫入失敗處理
   - 日誌記錄

### 公開介面

```cpp
class ResultsArchiver {
public:
    // CSV 匯出
    bool saveToCSV(const ShotResult& shot, 
                   const std::string& weather,
                   const std::string& time,
                   const std::string& distance,
                   const std::string& target);
    
    // JSON 匯出
    bool saveToJSON(const std::vector<ShotResult>& shots);
    
    // 檔名產生
    std::string generateCSVFilename(int shooter_id);
    
private:
    void ensureDirectoryExists(const std::string& path);
    std::string formatTimestamp();
};
```

### 不負責事項

- ❌ 射擊記錄管理（由 TrackerState 負責）
- ❌ 狀態查詢（由 TrackerState 負責）
- ❌ 檔案讀取/載入（僅負責寫入）

### 依賴關係

**依賴（輸入）:**
- std::filesystem (目錄操作)
- nlohmann/json (JSON 格式化)

**被依賴（輸出）:**
- IRTracker (呼叫儲存)
- HttpApiServer (儲存命令)

---

## 第三部分：模組依賴關係圖

### 依賴層級

```
Layer 0 (底層服務 - 無依賴):
├── CameraManager
├── Detector
├── CalibrationService
├── TrackerState
├── ResultsArchiver
└── TrackerConfiguration

Layer 1 (業務邏輯 - 依賴 Layer 0):
├── HitDetectionService (依賴: CalibrationService)
└── ConfigurationManager

Layer 2 (協調器 - 依賴 Layer 0, 1):
├── IRTracker (依賴: CameraManager, Detector, CalibrationService, 
│              HitDetectionService, TrackerState, ResultsArchiver)
└── HttpApiServer (依賴: ConfigurationManager, IRTracker)

Layer 3 (控制器 - 依賴 Layer 2):
└── SystemController (依賴: IRTracker, HttpApiServer)

Layer 4 (入口點 - 依賴所有):
└── main (依賴: 所有模組)
```

### 依賴規則

✅ **允許:**
- 高層 → 低層依賴
- 同層之間依賴（若有明確理由）

❌ **禁止:**
- 低層 → 高層依賴（違反依賴反轉原則）
- 循環依賴

---

## 第四部分：模組間通訊協議

### 1. ConfigurationManager ↔ HttpApiServer

**方向:** HttpApiServer → ConfigurationManager

**介面:**
```cpp
// HttpApiServer 呼叫
config_mgr.updateTargetSettings(key, settings);
settings = config_mgr.getTargetSettings(key);
config_mgr.saveSettings();
```

**資料流:**
1. HTTP 請求 → HttpApiServer 解析 JSON
2. HttpApiServer 呼叫 ConfigurationManager 更新
3. ConfigurationManager 驗證與儲存
4. ConfigurationManager 回傳新狀態
5. HttpApiServer 回應 JSON

### 2. ConfigurationManager ↔ IRTracker

**方向:** ConfigurationManager → IRTracker

**介面:**
```cpp
// ConfigurationManager 呼叫
TrackerConfiguration config = config_mgr.buildTrackerConfig();
tracker.applyConfiguration(config);
```

**資料流:**
1. ConfigurationManager 組裝 TrackerConfiguration 物件
2. IRTracker 接收並分派到各服務
3. CalibrationService, HitDetectionService 更新內部狀態

### 3. IRTracker ↔ CalibrationService

**方向:** IRTracker → CalibrationService (雙向)

**介面:**
```cpp
// IRTracker 呼叫
calibration_svc_->startCalibration(CameraId::A);
calibration_svc_->addCalibrationPoint(CameraId::A, point);
bool calibrated = calibration_svc_->isCalibrated(CameraId::A);

// HitDetectionService 呼叫
cv::Mat H = calibration_svc_->getHomography(CameraId::A);
```

### 4. IRTracker ↔ HitDetectionService

**方向:** IRTracker → HitDetectionService

**介面:**
```cpp
// IRTracker 呼叫
cv::Point2f transformed = hit_detection_svc_->transformCoordinate(pt, CameraId::A, shooter_id);
int shooter = hit_detection_svc_->calculateShooterId(pt.x, CameraId::A);
bool hit = hit_detection_svc_->isHit(transformed, shooter);
```

### 5. IRTracker ↔ TrackerState

**方向:** IRTracker → TrackerState (雙向)

**介面:**
```cpp
// IRTracker 呼叫 (寫入)
state_->addShot(shot);
state_->recordHit(shooter_id);
state_->knockdownTarget(shooter_id);

// HttpApiServer 呼叫 (讀取)
shots = state_->getLatestShots();
hits = state_->getHitCount(shooter_id);
down = state_->isTargetDown(shooter_id);
```

---

## 第五部分：重構檢查清單

### 階段 1: 基礎設施

- [ ] 創建所有新模組的 .hpp 和 .cpp 檔案
- [ ] 定義所有公開介面
- [ ] 建立 Makefile 規則

### 階段 2: 底層服務（可並行）

- [ ] 實作 CalibrationService
- [ ] 實作 TrackerState
- [ ] 實作 ResultsArchiver
- [ ] 實作 TrackerConfiguration

### 階段 3: 業務邏輯

- [ ] 實作 HitDetectionService
- [ ] 實作 ConfigurationManager

### 階段 4: 協調器

- [ ] 重構 IRTracker（移除職責到服務）
- [ ] 實作 HttpApiServer
- [ ] 實作 SystemController

### 階段 5: 入口點

- [ ] 精簡 main.cpp
- [ ] 連接所有模組

### 階段 6: 驗證

- [ ] 每個模組通過單一職責檢查
- [ ] 依賴方向正確（無循環）
- [ ] 所有功能保持不變
- [ ] 通過邊界驗證規則（SYSTEM_BOUNDARIES.md 第 7 節）

---

## 附錄：職責驗證工具

### 單一職責測試

對每個模組問以下問題：

```
□ 可以用一句話描述此模組的職責嗎？
□ 此模組只有一個修改理由嗎？
□ 可以獨立開發和測試嗎？
□ 介面方法都屬於同一職責嗎？
□ 若刪除此模組，只影響一種功能嗎？
```

**若任一答案為「否」，職責可能過多。**

### 介面隔離測試

```
□ 呼叫者是否會使用所有公開方法？
□ 介面方法數量是否 < 10 個？
□ 介面是否可拆分為更小的介面？
```

**若第一題為「否」，考慮拆分介面。**

---

**文件結束**

此文件符合 AI-Assisted Development Constitution v2.0 Article 3 要求。  
**每個模組必須有且僅有一個核心職責。**
