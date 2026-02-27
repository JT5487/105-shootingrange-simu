import pyautogui
import requests
import time
import threading

# --- 【雙投影幕設定區】 ---
# 如果您的兩個投影幕是「橫向併排」
# 例如 1920 + 1920 = 3840 寬度
TOTAL_WIDTH = 3840    # 請輸入兩個投影幕相加的總寬度
TOTAL_HEIGHT = 1080   # 請輸入投影幕的高度
X_OFFSET = 0          # 如果投影畫面不是從最左端開始，請調整此數值

HTTP_URL = "http://127.0.0.1:8080/shots" # Python 後端網址
SHOOTER_KEYS = {1: '1', 2: '2', 3: '3', 4: '4', 5: '5', 6: '6'}

# 關閉 pyautogui 的防呆機制 (因為雙螢幕座標可能會超出主螢幕範圍)
pyautogui.FAILSAFE = False 

print(f"🚀 雙投影幕橋接器啟動！")
print(f"目標範圍: {TOTAL_WIDTH}x{TOTAL_HEIGHT} (從 {X_OFFSET} 開始)")
print("--------------------------------------------------")

def poll_and_simulate():
    while True:
        try:
            response = requests.get(HTTP_URL, timeout=1)
            if response.status_code == 200:
                data = response.json()
                shots = data.get("shots", [])
                
                for shot in shots:
                    norm_x = shot.get("x")
                    norm_y = shot.get("y")
                    shooter_id = shot.get("shooter_id")
                    
                    # 轉換為【雙螢幕總寬度】的像素座標
                    screen_x = int(norm_x * TOTAL_WIDTH) + X_OFFSET
                    screen_y = int(norm_y * TOTAL_HEIGHT)
                    
                    print(f"🎯 命中！ 射手:{shooter_id} -> 螢幕點:({screen_x}, {screen_y})")
                    
                    # 執行模擬點擊
                    pyautogui.click(x=screen_x, y=screen_y)
                    
                    # 同時模擬鍵盤 1-6 號按鍵 (方便 UE5 用代碼接收)
                    if shooter_id in SHOOTER_KEYS:
                        pyautogui.press(SHOOTER_KEYS[shooter_id])
                        
        except Exception as e:
            # 這裡不顯示頻繁的錯誤，避免刷屏
            pass
            
        time.sleep(0.05) # 20Hz

if __name__ == "__main__":
    t = threading.Thread(target=poll_and_simulate)
    t.daemon = True
    t.start()
    input("橋接運行中！按下 Enter 鍵結束...\n")
