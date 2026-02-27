# T91 紅外線射擊追蹤系統 - 系統邊界規範

**版本:** 1.0  
**日期:** 2026-02-05  
**狀態:** 初始版本  
**依據:** AI-Assisted Development Constitution v2.0

---

## 文件目的

本文件定義 T91 紅外線射擊追蹤系統的系統邊界，明確說明：
- 什麼在系統內部（系統負責）
- 什麼在系統外部（外部依賴或不負責）
- 模組之間的介面契約
- 資料流向和控制流向

根據 AI 開發憲章 Article 2，本文件是**開始編碼前的必要文件**。

---

## 1. 系統邊界總覽

### 1.1 系統內部（System Inside）

T91 系統**負責**以下功能：

```
┌─────────────────────────────────────────────────────────────┐
│                    T91 射擊追蹤系統                          │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│  [1] 影像擷取與處理                                          │
│      - 從雙 Basler GigE 相機抓取影像                        │
│      - IR 雷射點偵測 (OpenCV)                               │
│      - 影像前處理 (閾值、形態學)                            │
│                                                             │
│  [2] 相機校準                                               │
│      - 4 點 homography 校準                                 │
│      - 校準資料持久化 (JSON)                                │
│      - 校準狀態管理                                         │
│                                                             │
│  [3] 座標轉換與命中判定                                      │
│      - 相機像素 → 正規化螢幕空間 [-1, 1]                    │
│      - 射手 ID 計算 (X 座標 → [1-6])                        │
│      - 垂直容差檢查 (依距離模式)                            │
│      - 重複射擊過濾                                         │
│                                                             │
│  [4] 目標與射手狀態管理                                      │
│      - 6 個射手的命中計數器                                 │
│      - 6 個標靶的倒下/站立狀態                              │
│      - 最近 50 筆射擊記錄                                   │
│      - 執行緒安全的狀態存取                                 │
│                                                             │
│  [5] 配置管理                                               │
│      - 目標調整參數 (scale, vertical, spacing, edgePadding) │
│      - 歸零偏移 (全域、模式、個別射手)                      │
│      - 環境設定 (天氣、時間、距離)                          │
│      - JSON 設定檔讀寫                                      │
│                                                             │
│  [6] HTTP API 伺服器                                        │
│      - RESTful 端點 (20+ 個)                                │
│      - JSON 請求/回應處理                                   │
│      - CORS 支援                                            │
│      - 應用程式生命週期控制                                 │
│                                                             │
│  [7] 結果匯出                                               │
│      - CSV 檔案匯出 (中文欄位)                              │
│      - JSON 結果儲存                                        │
│      - 除錯日誌寫入                                         │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

### 1.2 系統外部（System Outside）

以下**不是** T91 系統的職責：

```
┌─────────────────────────────────────────────────────────────┐
│                    外部系統與依賴                             │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│  [1] 前端 UI (不負責)                                       │
│      - Web 瀏覽器 (Chrome)                                  │
│      - HTML/CSS/JavaScript 前端應用                         │
│      - 使用者輸入驗證 (後端僅做基礎驗證)                    │
│      - UI 渲染與動畫                                        │
│                                                             │
│  [2] Unreal Engine 5 整合 (不負責)                          │
│      - UE5 場景渲染                                         │
│      - 3D 模型與物理模擬                                    │
│      - UE5 通過 HTTP 輪詢 /shots 端點                       │
│                                                             │
│  [3] 硬體驅動 (依賴外部)                                     │
│      - Basler Pylon SDK (相機驅動)                          │
│      - 作業系統 GigE 網路堆疊                               │
│      - 相機韌體與硬體控制                                   │
│                                                             │
│  [4] 檔案系統 (依賴外部)                                     │
│      - Windows 檔案系統 (讀寫權限由 OS 管理)                │
│      - 虛擬磁碟 X: (由外部 PowerShell 腳本掛載)             │
│      - 備份與還原機制                                       │
│                                                             │
│  [5] 成績管理 (不負責)                                       │
│      - 射擊成績的長期儲存                                   │
│      - 統計分析與報表生成                                   │
│      - 訓練歷史查詢                                         │
│                                                             │
│  [6] 安全與認證 (目前無)                                     │
│      - 使用者身份驗證                                       │
│      - 存取權限控制                                         │
│      - 稽核日誌                                             │
│      註: 這是已知的 CRITICAL-007 安全漏洞                    │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

---

## 2. 系統輸入/輸出邊界

### 2.1 輸入邊界（Inputs）

| 輸入來源 | 資料類型 | 介面 | 頻率 | 所有權 |
|---------|---------|------|------|--------|
| **Basler 相機 A** | 影像幀 (GigE) | Pylon SDK API | 45 fps | 外部硬體 |
| **Basler 相機 B** | 影像幀 (GigE) | Pylon SDK API | 45 fps | 外部硬體 |
| **HTTP 用戶端** | JSON 請求 | POST /command | 按需 | 外部應用 (Web/UE5) |
| **設定檔** | JSON 檔案 | 檔案系統 | 啟動時 + 變更時 | 系統內部 |
| **校準檔** | JSON 檔案 | 檔案系統 | 啟動時 | 系統內部 |

**輸入驗證規則:**
- 相機影像：必須是有效的 GigE 封包，Pylon SDK 負責驗證
- HTTP 請求：**目前缺乏驗證** (CRITICAL-007, HIGH-013)
- JSON 檔案：使用 nlohmann/json 解析，格式錯誤會拋出例外

### 2.2 輸出邊界（Outputs）

| 輸出目標 | 資料類型 | 介面 | 頻率 | 所有權 |
|---------|---------|------|------|--------|
| **HTTP 用戶端** | JSON 回應 | POST /command 回應 | 按需 | 系統內部 |
| **射擊記錄檔** | CSV 檔案 | 檔案系統 (../射擊成績/) | 每次射擊 | 系統內部 |
| **最後結果檔** | JSON 檔案 | last_results.json | 每次射擊 | 系統內部 |
| **除錯日誌** | 文字檔案 | t91_debug.log | 持續 | 系統內部 |
| **控制台輸出** | stdout/stderr | 終端機 | 持續 | 開發用 |

**輸出保證:**
- HTTP 回應：永遠是有效的 JSON 格式
- CSV 檔案：UTF-8 BOM 編碼，中文欄位
- JSON 檔案：符合 nlohmann/json 格式

---

## 3. 模組職責矩陣

### 3.1 當前架構（現狀）

| 模組 | 職責 | 檔案 | 行數 | 狀態 |
|------|------|------|------|------|
| **main.cpp** | HTTP API + 配置 + 生命週期 + 業務邏輯 | cpp/src/main.cpp | 438 | ⚠️ 違反單一職責 |
| **IRTracker** | 影像處理 + 校準 + 命中判定 + 配置 + 狀態 + I/O | cpp/src/IRTracker.cpp | ~1000 | ⚠️ 違反單一職責 |
| **CameraManager** | 相機硬體抽象 | cpp/src/CameraManager.cpp | ~200 | ✅ 職責單一 |
| **Detector** | IR 點偵測 + 診斷日誌 | cpp/include/Detector.hpp | 72 | ⚠️ 輕微混合 |

**問題:**
- main.cpp 和 IRTracker 違反憲章 Article 3（單一職責原則）
- 難以獨立測試各職責
- 修改一個功能可能影響其他功能

### 3.2 目標架構（重構後）

| 模組 | 單一職責 | 公開介面 | 依賴 | 狀態 |
|------|---------|---------|------|------|
| **main** | 應用程式入口點與組裝 | main() | 所有其他模組 | 🎯 目標 |
| **ConfigurationManager** | 配置管理與持久化 | loadSettings(), saveSettings(), applyToTracker() | 無 | 🎯 目標 |
| **HttpApiServer** | HTTP 請求路由與處理 | start(), stop(), registerEndpoints() | ConfigurationManager | 🎯 目標 |
| **SystemController** | 生命週期管理 | initialize(), shutdown(), launchUI() | IRTracker, HttpApiServer | 🎯 目標 |
| **IRTracker** | 高階影像處理協調 | start(), stop(), processingLoop() | CameraManager, Services | 🎯 目標 |
| **CalibrationService** | 相機校準邏輯 | addCalibrationPoint(), getHomography() | 無 | 🎯 目標 |
| **HitDetectionService** | 命中判定與座標轉換 | transformCoordinate(), isHit() | CalibrationService | 🎯 目標 |
| **TrackerState** | 執行緒安全狀態管理 | addShot(), recordHit(), getLatestShots() | 無 | 🎯 目標 |
| **ResultsArchiver** | 結果匯出 | saveToCSV(), saveToJSON() | 無 | 🎯 目標 |
| **CameraManager** | 相機硬體抽象 | grab(), open(), close() | Pylon SDK | ✅ 已存在 |
| **Detector** | IR 點偵測演算法 | detect() | OpenCV | ✅ 已存在 |

---

## 4. 跨模組介面契約

### 4.1 介面定義原則

根據憲章 Article 3 和 5：
- 模組間**僅通過公開介面**互動
- **禁止**直接存取其他模組的私有成員
- **禁止**假設其他模組的內部實作

### 4.2 核心介面契約

#### 4.2.1 ICamera 介面（已存在）

```cpp
// 位置: cpp/include/CameraManager.hpp
class ICamera {
public:
    virtual bool open() = 0;
    virtual bool grab(cv::Mat& frame) = 0;
    virtual void close() = 0;
    virtual ~ICamera() = default;
};
```

**契約:**
- `open()`: 必須在首次 `grab()` 前呼叫，回傳 true 表示成功
- `grab()`: 回傳 true 且 frame 包含有效影像，或回傳 false
- `close()`: 可重複呼叫，必須釋放所有硬體資源
- 執行緒安全：**不保證**，呼叫者需自行同步

#### 4.2.2 IRTracker 公開介面（當前）

```cpp
// 位置: cpp/include/IRTracker.hpp
class IRTracker {
public:
    // 生命週期
    void start();
    void stop();
    
    // 狀態查詢
    std::vector<ShotResult> getLatestShots();
    bool isTargetDown(int shooter_id);
    int getHitCount(int shooter_id);
    
    // 命令
    void knockdownTarget(int shooter_id);
    void resetTarget(int shooter_id);
    void registerHit(int shooter_id);
    
    // 校準
    void startCalibrationA();
    void startCalibrationB();
    
    // 配置 (20+ setter 方法)
    void setTargetAdjustments(...);
    void setZeroingOffsets(...);
    // ...
};
```

**問題:**
- 介面過於龐大（違反介面隔離原則）
- 混合了多種職責的方法

#### 4.2.3 建議的服務介面（目標架構）

**ConfigurationManager 介面:**
```cpp
class ConfigurationManager {
public:
    // 載入與儲存
    bool loadSettings();
    bool saveSettings();
    
    // 查詢
    TargetSettings getTargetSettings(const std::string& key);
    ShooterConfig getShooterConfig(int shooter_id);
    
    // 更新
    void updateTargetSettings(const std::string& key, const TargetSettings& settings);
    void updateShooterConfig(int shooter_id, const std::string& field, const json& value);
    
    // 同步到追蹤器
    void applyToTracker(IRTracker& tracker);
};
```

**CalibrationService 介面:**
```cpp
class CalibrationService {
public:
    // 校準流程
    void startCalibration(CameraId camera);
    bool addCalibrationPoint(CameraId camera, cv::Point2f point);
    bool computeHomography(CameraId camera);
    
    // 查詢
    cv::Mat getHomography(CameraId camera) const;
    bool isCalibrated(CameraId camera) const;
    
    // 持久化
    bool loadCalibration(const std::string& filepath);
    bool saveCalibration(const std::string& filepath);
};
```

**HitDetectionService 介面:**
```cpp
class HitDetectionService {
public:
    // 座標轉換
    cv::Point2f transformCoordinate(cv::Point2f camera_point, 
                                    const cv::Mat& homography,
                                    const ZeroingOffsets& offsets);
    
    // 射手 ID 計算
    int calculateShooterId(float normalized_x, CameraId camera);
    
    // 命中判定
    bool isHit(cv::Point2f normalized_point, 
               const std::string& distance_mode,
               const TargetAdjustments& adjustments);
    
    // 重複過濾
    bool isDuplicate(const ShotResult& shot, 
                     const std::vector<ShotResult>& recent_shots);
};
```

---

## 5. 外部依賴清單

### 5.1 硬體依賴

| 依賴項 | 版本 | 用途 | 介面 | 可替換性 |
|-------|------|------|------|---------|
| **Basler 相機 A** | acA1920-40gm | 左側 3 個射手追蹤 | GigE | 低 (硬體固定) |
| **Basler 相機 B** | acA1920-40gm | 右側 3 個射手追蹤 | GigE | 低 (硬體固定) |
| **GigE 網路** | 1 Gbps | 相機資料傳輸 | RJ45 | 中 (需高速網路) |

**假設與限制:**
- 相機序號：A=23058324, B=23058325 (硬編碼於 IRTracker.cpp:19-20)
- 網路頻寬：需支援 2 相機 × 45fps × ~2MB/幀 = ~180 MB/s
- 電源：相機需 PoE (Power over Ethernet) 或外部電源

### 5.2 軟體依賴

| 依賴項 | 版本 | 用途 | 授權 | 介面 |
|-------|------|------|------|------|
| **Basler Pylon SDK** | 6.x+ | 相機驅動與影像擷取 | 商業 | C++ API |
| **OpenCV** | 4.x | 影像處理與 homography | BSD | C++ API |
| **nlohmann/json** | 3.x | JSON 解析與序列化 | MIT | Header-only |
| **cpp-httplib** | 0.x | HTTP 伺服器 | MIT | Header-only |
| **C++ 標準庫** | C++17 | 執行緒、檔案 I/O、容器 | 標準 | 標準 API |

**編譯器要求:**
- g++ (MinGW-w64) with `-std=c++17`
- 支援 `-O3` 最佳化
- 條件編譯：`-DUSE_PYLON` (啟用 Basler SDK)

### 5.3 作業系統依賴

| 依賴項 | 平台 | 用途 | 介面 |
|-------|------|------|------|
| **Windows 10/11** | x64 | 主要作業系統 | Win32 API |
| **檔案系統** | NTFS | 設定檔與結果儲存 | C++ std::filesystem |
| **Process API** | Windows | Chrome 啟動與關閉 | system() 呼叫 |
| **網路堆疊** | TCP/IP | GigE 相機與 HTTP 伺服器 | Socket API |

**作業系統假設:**
- `taskkill.exe` 存在於 `C:\Windows\System32\`
- Chrome 安裝於預設路徑
- 具備讀寫 `../射擊成績/` 目錄的權限

---

## 6. 資料流向圖

### 6.1 主要資料流

```
[Basler 相機 A/B]
         │ GigE 影像幀 (45 fps)
         ↓
   [CameraManager]
         │ cv::Mat
         ↓
     [Detector]
         │ IR 點座標 (像素)
         ↓
   [IRTracker::processingLoop]
         │
         ├─→ [CalibrationService] (校準模式)
         │       │ 4 個校準點
         │       ↓ cv::findHomography()
         │   [Homography 矩陣]
         │
         └─→ [HitDetectionService] (追蹤模式)
                 │ 應用 homography
                 ↓
             [正規化座標 -1~1]
                 │ 應用歸零偏移
                 ↓
             [射手 ID + 命中判定]
                 │
                 ↓
           [TrackerState]
                 │ 更新 hits_, targets_down_
                 │ 加入 latest_shots_
                 ↓
         [ResultsArchiver]
                 │
                 ├─→ CSV 檔案 (../射擊成績/)
                 └─→ JSON 檔案 (last_results.json)
```

### 6.2 控制流向

```
[HTTP Client (Web/UE5)]
         │ POST /command
         ↓
   [HttpApiServer]
         │ 解析 action
         ├─→ "adjust_targets" → [ConfigurationManager]
         │                           │ 更新設定
         │                           ↓ applyToTracker()
         │                       [IRTracker]
         │
         ├─→ "knockdown_target" → [IRTracker::knockdownTarget()]
         │                            │
         │                            ↓
         │                      [TrackerState] 更新狀態
         │
         ├─→ "start_calibration_a" → [IRTracker::startCalibrationA()]
         │                               │
         │                               ↓
         │                         [CalibrationService] 進入校準模式
         │
         └─→ "shutdown_system" → [SystemController::shutdown()]
                                      │ ⚠️ 無認證 (CRITICAL-007)
                                      ↓
                                  強制關閉 Chrome + 後端
```

### 6.3 初始化流程

```
[main()]
    │
    ├─→ [ConfigurationManager::loadSettings()]
    │       │ 讀取 target_settings.json
    │       ↓
    │   [全域配置變數]
    │
    ├─→ [IRTracker 建構]
    │       │ 建立 CameraManager (BaslerCamera)
    │       │ 載入校準檔 (calibration_a.json, calibration_b.json)
    │       ↓
    │   [IRTracker 物件]
    │
    ├─→ [IRTracker::start()]
    │       │ 啟動 processingLoop 執行緒
    │       ↓
    │   [背景影像處理]
    │
    ├─→ [HttpApiServer 建構]
    │       │ 註冊所有端點處理器
    │       ↓
    │   [httplib::Server]
    │
    ├─→ [啟動 Chrome]
    │       │ system() 呼叫
    │       ↓
    │   [Web UI]
    │
    └─→ [svr.listen("0.0.0.0", 8081)]
            │ 阻塞主執行緒
            ↓
        [等待 HTTP 請求...]
```

---

## 7. 邊界違規檢查清單

### 7.1 當前邊界違規

根據憲章 Article 1-3，以下是當前的邊界違規：

| 違規 ID | 類型 | 位置 | 描述 | 嚴重性 |
|---------|------|------|------|--------|
| **BV-001** | 職責邊界 | main.cpp | 混合 4 個職責 | CRITICAL |
| **BV-002** | 職責邊界 | IRTracker.cpp | 混合 6 個職責 | HIGH |
| **BV-003** | 模組耦合 | main.cpp:19-20 | 直接存取 IRTracker 內部 | MEDIUM |
| **BV-004** | 介面污染 | IRTracker.hpp | 20+ 個 setter 方法 | MEDIUM |
| **BV-005** | 跨邊界存取 | main.cpp:99-104 | 重複呼叫 setTargetAdjustments() | LOW |

### 7.2 邊界驗證規則

未來開發時，所有新程式碼必須通過以下檢查：

**Rule 1: 單一職責檢查**
```
□ 此模組/類別只有一個修改理由？
□ 可以用一句話描述其職責嗎？
□ 是否可獨立測試此職責？
```

**Rule 2: 介面隔離檢查**
```
□ 公開介面的每個方法都屬於同一職責嗎？
□ 呼叫者是否需要依賴不使用的方法？
□ 介面方法數量是否 < 10 個？
```

**Rule 3: 依賴方向檢查**
```
□ 高階模組是否依賴低階模組？
□ 是否通過介面（抽象）依賴？
□ 是否存在循環依賴？
```

**Rule 4: 資料所有權檢查**
```
□ 每個資料結構是否有唯一擁有者？
□ 其他模組是否通過介面存取？
□ 是否正確使用 const 保護唯讀存取？
```

---

## 8. 遷移計劃（從當前到目標架構）

### 8.1 階段 1: 文件化當前邊界（已完成）

- ✅ 本文件 (SYSTEM_BOUNDARIES.md)
- ⏳ INVARIANTS.md (下一步)
- ⏳ MODULE_BOUNDARIES.md (下一步)
- ⏳ KNOWN_RISKS.md (下一步)

### 8.2 階段 2: 重構 main.cpp（優先度：HIGH）

**目標:** 分離 4 個職責為獨立模組

**步驟:**
1. 抽取 ConfigurationManager (16-81 行)
2. 抽取 HttpApiServer (131-427 行)
3. 抽取 SystemController (83-130 行)
4. 精簡 main() 為組裝器 (~50 行)

**驗證:**
- 每個新模組通過單一職責檢查
- 所有現有功能保持不變
- 通過邊界驗證規則

### 8.3 階段 3: 重構 IRTracker（優先度：HIGH）

**目標:** 分離 6 個職責為服務類別

**步驟:**
1. 抽取 CalibrationService
2. 抽取 HitDetectionService
3. 抽取 TrackerConfiguration
4. 抽取 TrackerState
5. 抽取 ResultsArchiver
6. IRTracker 簡化為協調器

**驗證:**
- 每個服務通過介面隔離檢查
- 依賴方向正確（高階 → 低階）
- 無循環依賴

### 8.4 階段 4: 介面標準化（優先度：MEDIUM）

**目標:** 所有模組間通過明確介面互動

**步驟:**
1. 定義所有服務介面（.hpp）
2. 實作介面實現（.cpp）
3. 移除直接耦合
4. 使用依賴注入

---

## 9. 附錄：術語表

| 術語 | 定義 |
|------|------|
| **系統邊界** | 系統負責與不負責的功能分界線 |
| **模組** | 具有單一職責的程式碼單元 (.cpp/.hpp) |
| **介面契約** | 模組間互動的明確規範 |
| **資料流** | 資料在模組間的傳遞路徑 |
| **控制流** | 程式執行順序與決策邏輯 |
| **外部依賴** | 系統依賴但不控制的外部資源 |
| **邊界違規** | 違反單一職責或介面隔離的程式碼 |
| **職責** | 模組變更的單一理由 |
| **耦合** | 模組間的依賴程度 |
| **內聚** | 模組內部元素的相關性 |

---

## 10. 維護與更新

**文件所有者:** 架構師 / 技術負責人

**更新觸發條件:**
- 新增主要模組或服務
- 變更外部依賴（SDK、函式庫）
- 重構導致邊界變動
- 發現新的邊界違規

**更新頻率:**
- 每次重大重構後
- 每季度審查一次
- 發現違規時立即更新

**版本歷史:**
- v1.0 (2026-02-05): 初始版本，記錄當前架構與目標架構

---

**文件結束**

此文件符合 AI-Assisted Development Constitution v2.0 Article 2 要求。  
在進行任何架構變更前，必須先更新此文件。
