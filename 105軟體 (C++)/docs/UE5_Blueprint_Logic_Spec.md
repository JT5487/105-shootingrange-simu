# UE5 藍圖邏輯詳細規格書 (Blueprint Logic Spec)

## 一、 概觀
本文件提供給 UE5 開發人員，用於實作 T91 射擊訓練系統的藍圖邏輯。系統採用 **HTTP 輪詢方案** 與 Python 後端通訊。

---

## 二、 全域資料類型 (Enums & Structs)

### 2.1 E_ShootingMode
- `25`: 25公尺定靶
- `75`: 75公尺定靶
- `175`: 175公尺定靶
- `dynamic`: 動態射擊模式

### 2.2 E_KnockdownMode
- `on_hit`: 一中即倒
- `on_time`: 定時倒靶 (如 3 秒後自動立起)
- `on_count`: 累計射擊 (如中 3 發才倒)
- `manual`: 手動控制

---

## 三、 GM_ShootingRange (Game Mode) 核心邏輯

### 3.1 變數清單
| 變數名稱 | 類型 | 說明 |
| :--- | :--- | :--- |
| `AllTargets` | Actor Array (BP_Target_Base) | 場景中 1-6 號靶的引用 |
| `HitCounts` | Integer Array (Size 6) | 紀錄六位射手的總命中數 |
| `CurrentState` | Struct | 儲存從 `/state` 輪詢回來的環境狀態物件 |

### 3.2 Event Graph: 初始化與輪詢
1. **Event BeginPlay**:
    - `Get All Actors of Class (BP_Target_Base)` -> 填充 `AllTargets` 陣列。
    - `Create User Widget (WBP_HUD)` -> `Add to Viewport`。
    - `Set Timer by Event`:
        - **Event**: `PollDataFromServer`
        - **Time**: 0.05 秒 (20 Hz)
        - **Looping**: True

2. **Custom Event: PollDataFromServer**:
    - 使用 `HTTP Request` (GET) 呼叫 `http://127.0.0.1:8080/shots`。
    - **On Complete**:
        - 解析 JSON 內容中的 `shots` 陣列。
        - **For Each Loop** (針對每個 shot 物件):
            - 提取 `shooter_id` (1-6), `x` (0-1), `y` (0-1)。
            - 呼叫函數 `DistributeShot(ID, X, Y)`。

### 3.3 函數: DistributeShot (分配射擊點)
- **邏輯**:
    - 遍歷 `AllTargets`。
    - 呼叫 `TargetRef -> CheckHit(X, Y)`。
    - **If True**:
        - `HitCounts[ID-1]++`。
        - 呼叫 `TargetRef -> PlayHit(ID)`。
        - **Break Loop** (防止一發多中)。

---

## 四、 BP_Target_Base (靶標母類別) 核心邏輯

### 4.1 變數清單
| 變數名稱 | 類型 | 預設值 | 說明 |
| :--- | :--- | :--- | :--- |
| `TargetID` | Integer | 1-6 | 對應射手編號 |
| `MinX / MaxX` | Float | 0.0 / 1.0 | 該靶在投影映射中的 X 範圍 |
| `MinY / MaxY` | Float | 0.0 / 1.0 | 該靶在投影映射中的 Y 範圍 |
| `bIsStanding` | Boolean | True | 紀錄目前靶子是否立起 |

### 4.2 函數: CheckHit (判定命中)
- **輸入**: `InX`, `InY` (Float)
- **邏輯**:
    - `Branch`: `bIsStanding` 是否為 True？
    - `AND`: `InX` 是否在 `[MinX, MaxX]`？
    - `AND`: `InY` 是否在 `[MinY, MaxY]`？
    - **回傳**: 布林值。

### 4.3 Custom Event: PlayHit
1. **Spawn Sound at Location**: 播放 `SFX_MetalImpact`。
2. **Spawn System at Location**: 在靶標 Mesh 中心播放 `NS_BulletImpact` (Niagara)。
3. **Branch**: `KnockdownMode == on_hit`?
    - **True**: 執行 `Timeline: KnockdownRotate`。

### 4.4 Timeline: KnockdownRotate
- **長度**: 0.8 秒。
- **軌道**: Float Track (0->1.0)。
- **Update**: `Set Relative Rotation` (Pitch 從 0 變為 -90)。
- **Finished**: `Set bIsStanding = False`。
- **延遲 (如 3秒)** -> 執行立靶動畫 -> `Set bIsStanding = True`。

---

## 五、 環境與模式同步 (State Synchronization)

### 5.1 Event: SyncState
- 建議每 1.0 秒輪詢一次 `http://127.0.0.1:8080/state`。
- **解析 JSON**:
    - `weather`: 切換 `BP_EnvironmentManager` 的晴/雨模式。
    - `mode`: 
        - 若為 `25`, `75`, `175`: 調整所有靶標的 `World Scale` (如 25m=1.0, 175m=0.14)。
        - 若為 `dynamic`: 開啟靶標的橫向移動 Timeline。

---

## 六、 提示與建議
1. **座標系**: Python 送出的 X=0 是畫面最左側，X=1 是最右側。UE 開發者需確保 6 個靶標的 `MinX/MaxX` 剛好填滿 0.0 到 1.0 區間。
2. **性能**: `GET /shots` 必須低延遲。如果輪詢造成卡頓，請將 HTTP 請求放入非同步線程或優化 JSON 解析。
3. **偵錯**: 在場景中放置一個小的 Debug Sphere，在每次收到 `shots` 時 Spawn 在對應的 3D 座標上，方便校準。

---
**文件結束**
