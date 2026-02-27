# UE5 + VaRest 射擊系統藍圖開發對照清單 (Checklist)

## 一、 開發環境準備
- [ ] 已從 Epic Marketplace 安裝 **VaRest** 插件。
- [ ] 專案設定 (Edit -> Plugins) 已勾選 **VaRest** 並重啟引擎。
- [ ] Python 後端 (`ir_tracker_basler.py`) 已啟動且 Port 8080 正常運作。

---

## 二、 Game Mode (數據監聽與分發)

### 2.1 關鍵變數 (Variables)
| 變數名稱 | 類型 | 說明 |
| :--- | :--- | :--- |
| `Timer_PollShots` | Timer Handle | 每 0.05 秒觸發一次的計時器控制 |
| `AllTargets` | Actor Array (BP_Target_Base) | 儲存場景內 6 個射手靶位的實體引用 |
| `URL_GetShots` | String | 設定為 `http://127.0.0.1:8080/shots` |
| `URL_GetState` | String | 設定為 `http://127.0.0.1:8080/state` |

### 2.2 核心事件與節點 (Events & Nodes)
- **Event BeginPlay**: 初始化 `Timer_PollShots` 節點。
- **Custom Event: RequestShotsFromServer**:
    - `Construct Json Request`
    - `Set Verb` (GET)
    - `Process Request` (綁定 OnComplete 事件)
- **Json 解析路徑節點**:
    - `Get Root Object`
    - `Get Array Field` (欄位名稱: `shots`)
    - `For Each Loop`
    - `As Object` -> `Get Integer Field` (`shooter_id`)
    - `As Object` -> `Get Number Field` (`x`)
    - `As Object` -> `Get Number Field` (`y`)

---

## 三、 BP_Target_Base (靶位判定)

### 3.1 屬性設定變數 (請設為 Editable / Instance Editable)
| 變數名稱 | 類型 | 說明 |
| :--- | :--- | :--- |
| `TargetID` | Integer | 設定 1~6，對應射手編號 |
| `MinX` | Float | 判定區最左側 (0.0~1.0) |
| `MaxX` | Float | 判定區最右側 (0.0~1.0) |
| `MinY` | Float | 判定區最頂端 (0.0~1.0) |
| `MaxY` | Float | 判定區最底端 (0.0~1.0) |
| `bIsStanding` | Boolean | 紀錄靶子目前是否立起 (預設 True) |

### 3.2 命中判定函數 (Function: CheckHit)
- **輸入**: `InX` (Float), `InY` (Float)
- **邏輯節點組件**:
    - `InRange (Float)`: 檢查 X 是否在 MinX 與 MaxX 之間。
    - `InRange (Float)`: 檢查 Y 是否在 MinY 與 MaxY 之間。
    - `Branch`: 同時符合 X 範圍、Y 範圍且 `bIsStanding == True`。
- **回傳**: 當上述條件成立時，回傳 `True`。

---

## 四、 預期數據欄位對照表 (JSON Mapping)

在使用 VaRest 的 `Get Field` 相關節點時，字串必須完全精確：

| 類別 | VaRest 節點輸入字串 | 說明 |
| :--- | :--- | :--- |
| **射擊** | `"shots"` | 彈孔列表 (Array) |
| **射手** | `"shooter_id"` | 槍枝編號 (1~6) |
| **座標** | `"x"` | 正規化 X (0.0 ~ 1.0) |
| **座標** | `"y"` | 正規化 Y (0.0 ~ 1.0) |
| **天氣** | `"weather"` | 狀態值: `sunny` 或 `rainy` |
| **距離** | `"mode"` | 狀態值: `25`, `75`, `175`, `dynamic` |
| **倒靶** | `"knockdown"` | 狀態值: `on_hit`, `manual`, 等 |

---

## 五、 6 個靶位分配建議 (X 軸範圍)
若您的畫面水平填滿，請參考以下預設值微調：

- **射手 1**: MinX: 0.00, MaxX: 0.16
- **射手 2**: MinX: 0.17, MaxX: 0.33
- **射手 3**: MinX: 0.34, MaxX: 0.50
- **射手 4**: MinX: 0.51, MaxX: 0.67
- **射手 5**: MinX: 0.68, MaxX: 0.84
- **射手 6**: MinX: 0.85, MaxX: 1.00

---

## 六、 常用音效與特效路徑預留
- **命中音效**: `/Game/Audio/SFX_MetalHit`
- **中彈粒子**: `/Game/Effects/PS_BulletImpact`
- **倒靶動畫**: `/Game/Animations/Timeline_Rotate90`

---
**文件結束 - 2025-12-20**
