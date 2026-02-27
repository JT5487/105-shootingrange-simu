# UE5 Blueprint 實作指南 - T91 射擊訓練系統 (v2.0)

## 版本資訊
- 引擎版本: UE5.4.4
- 文件版本: 2.0
- 更新日期: 2025-12-20
- 系統架構: Python (OpenCV/Basler) ↔ HTTP Server (Port 8080) ↔ UE5 & Web Control

---

## 一、 通訊協定架構

系統現在統一使用 **HTTP (Port 8080)** 作為主要的通訊介面。

- **Web Control Panel (index.html)**: 使用 `POST /command` 發送參數。
- **UE5 Engine**: 
    1. 使用 `GET /shots` 輪詢 (Poll) 最新彈孔資料。
    2. 使用 `GET /state` 獲取當前環境與模式狀態（天候、距離、倒靶模式）。

### 1.1 Python 後端整合 (ir_tracker_basler.py)

在您的 `CommandHandler` 類別中，應實作以下邏輯以支援 UE5：

```python
# 在 CommandHandler 類別中新增 do_GET
class CommandHandler(BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path == '/shots':
            # 傳回自上次讀取後的所有彈孔座標
            self.send_response(200)
            self.send_header('Content-Type', 'application/json')
            self.send_header('Access-Control-Allow-Origin', '*')
            self.end_headers()
            
            # 從 tracker 获取彈孔隊列
            shots = []
            while not self.server.tracker.shot_queue.empty():
                shots.append(self.server.tracker.shot_queue.get())
            
            self.wfile.write(json.dumps({"shots": shots}).encode())
            
        elif self.path == '/state':
            # 傳回目前系統設定 (由 Web 端設定的內容)
            self.send_response(200)
            self.send_header('Content-Type', 'application/json')
            self.send_header('Access-Control-Allow-Origin', '*')
            self.end_headers()
            
            state = {
                "weather": self.server.tracker.current_weather,
                "time": self.server.tracker.current_time,
                "mode": self.server.tracker.current_mode,
                "knockdown": self.server.tracker.knockdown_logic
            }
            self.wfile.write(json.dumps(state).encode())
```

---

## 二、 核心 Blueprint 修改

### 2.1 E_ShootingMode (列舉型別)
應與前端 `index.html` 的 ID 完全對應：
- `25` (25公尺定靶)
- `75` (75公尺定靶)
- `175` (175公尺定靶)
- `dynamic` (動態射擊模式)

### 2.2 E_KnockdownMode (列舉型別)
對應前端 `knockdown` 選項：
- `on_hit` (一中即倒)
- `on_time` (秒數倒靶)
- `on_count` (次數倒靶)
- `manual` (手動控制)

### 2.3 GM_ShootingRange 輪詢調整
在 `Event Tick` 或計時器中循環呼叫：
1. **获取彈孔**: `GET http://localhost:8080/shots`
   - 解析內含的 `shooter_id` 及 `x, y` 座標。
   - `x, y` 為 0.0 ~ 1.0 的正規化座標。
2. **獲取狀態**: `GET http://localhost:8080/state` (建議每秒一次即可)
   - 根據 `weather` 切換場景特效（Sun/Rain）。
   - 根據 `mode` 調整靶子大小（25m: Scale 1.0, 75m: 0.33, 175m: 0.14）。

---

## 三、 座標與靶位判斷

前端定義了 6 個射手位。UE5 的投影平面應劃分為對應區域：

### 3.1 命中判定區域建議 (X軸正規化座標)
| 射手編號 | X 軸中心點 | 判定範圍 (建議) |
| :--- | :--- | :--- |
| **射手 1** | 0.08 | 0.00 ~ 0.16 |
| **射手 2** | 0.25 | 0.17 ~ 0.33 |
| **射手 3** | 0.41 | 0.34 ~ 0.49 |
| **射手 4** | 0.58 | 0.50 ~ 0.66 |
| **射手 5** | 0.75 | 0.67 ~ 0.83 |
| **射手 6** | 0.91 | 0.84 ~ 1.00 |

*註：如果使用雙投影機拼接，Python 端會完成影像縫合，UE5 接收到的 X 座標跨度仍為 0.0 (左) 到 1.0 (右)。*

---

## 四、 特效與動畫

### 4.1 中彈特效 (Niagara)
- **觸發條件**: 當 `GET /shots` 收到數據，且座標落在任一 `BP_Target_Base` 的碰撞盒內。
- **音效**: 播放金屬撞擊聲 (SFX_MetalHit)。

### 4.2 倒靶動作
- 當 `KnockdownMode` 為 `on_hit`：
  - 中彈後直接呼叫 Timeline 旋轉靶板至 -90 度。
  - 等待 `3秒` (或教官點擊立靶) 後恢復 0 度。

---

## 五、 手動口令與演練同步

當教官在網頁點擊「訓練開始」或「SOP 流程」時，Python 會收到 `action: "sop_command"`。

- **UE5 動畫配合**:
  - 當收到 "開始射擊！" 時，啟動移動靶動畫（若是 `dynamic` 模式）。
  - 當收到 "停止射擊！" 或 "射手起立！" 時，強制所有靶子立起並停止移動。

---

## 六、 效能建議

1. **關閉 Lumen**: 射擊訓練場景燈光固定，使用傳統光照圖 (Lightmap) 或簡單動態光即可。
2. **影格率鎖定**: 建議鎖定在 `60 FPS`，確保輪旬頻率與畫面更新同步。
3. **UI 優化**: 主畫面 HUD 僅顯示 6 個射手的當前中彈數。

---

**文件結束**
