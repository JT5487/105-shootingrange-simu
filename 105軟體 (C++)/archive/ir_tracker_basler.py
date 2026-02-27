# -*- coding: utf-8 -*-
"""
T91 紅外線射擊訓練系統 - IR 追蹤程式
適用於 Basler acA640-90gm 工業攝影機

功能：
1. 連接 2 台 Basler GigE 攝影機
2. 偵測 850nm IR 光點
3. 座標轉換（攝影機座標 → 投影幕座標）
4. 分區判定射手 (1-6)
5. 開槍判定 + 播放槍聲
6. TCP 傳送座標給 UE5 / 網頁

作者: Claude AI Assistant
日期: 2025-12-20
"""

import cv2
import numpy as np
import json
import time
import threading
import socket
import queue
from dataclasses import dataclass, field
from typing import Optional, Tuple, List, Dict
from enum import Enum
import logging
import sqlite3
from datetime import datetime
from http.server import HTTPServer, BaseHTTPRequestHandler

# 設定日誌
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(name)s - %(levelname)s - %(message)s'
)
logger = logging.getLogger('IRTracker')

# ============== 設定 ==============

@dataclass
class CameraConfig:
    """攝影機設定"""
    id: str                          # "A" 或 "B"
    serial_number: str = ""          # Basler 序號（如果有多台需指定）
    width: int = 640
    height: int = 480
    fps: int = 90
    exposure_time: float = 1000.0    # 微秒
    gain: float = 10.0

@dataclass
class DetectionConfig:
    """偵測設定"""
    threshold: int = 200             # 亮度閾值 (0-255)
    min_area: int = 3                # 最小光點面積
    max_area: int = 500              # 最大光點面積
    trigger_frames: int = 1          # 連續偵測幀數才觸發

@dataclass
class NetworkConfig:
    """網路設定"""
    tcp_host: str = "0.0.0.0"
    tcp_port_ue5: int = 7000
    tcp_port_web_control: int = 7001
    tcp_port_web_score: int = 7002

@dataclass
class Config:
    """主設定"""
    camera_a: CameraConfig = field(default_factory=lambda: CameraConfig(id="A"))
    camera_b: CameraConfig = field(default_factory=lambda: CameraConfig(id="B"))
    detection: DetectionConfig = field(default_factory=DetectionConfig)
    network: NetworkConfig = field(default_factory=NetworkConfig)
    
    # 校準檔案路徑
    calibration_file_a: str = "calibration_a.json"
    calibration_file_b: str = "calibration_b.json"
    
    # 音效檔案
    gunshot_sound: str = "sounds/gunshot.wav"

config = Config()

# ============== 校準系統 ==============

class CalibrationSystem:
    """四點校準系統"""
    
    def __init__(self, camera_id: str):
        self.camera_id = camera_id
        self.src_points: List[List[float]] = []  # 攝影機座標
        self.dst_points: List[List[float]] = [   # 投影幕座標（正規化，中心為原點）
            [-1.0,  1.0],   # 左上
            [ 1.0,  1.0],   # 右上
            [ 1.0, -1.0],   # 右下
            [-1.0, -1.0],   # 左下
        ]
        self.homography_matrix: Optional[np.ndarray] = None
        self.is_calibrated: bool = False
    
    def add_calibration_point(self, raw_x: int, raw_y: int) -> int:
        """新增校準點，返回已收集的點數"""
        self.src_points.append([float(raw_x), float(raw_y)])
        logger.info(f"攝影機 {self.camera_id} 校準點 {len(self.src_points)}/4: ({raw_x}, {raw_y})")
        
        if len(self.src_points) == 4:
            self._calculate_homography()
        
        return len(self.src_points)
    
    def _calculate_homography(self):
        """計算透視轉換矩陣"""
        src = np.array(self.src_points, dtype=np.float32)
        dst = np.array(self.dst_points, dtype=np.float32)
        
        self.homography_matrix, _ = cv2.findHomography(src, dst)
        self.is_calibrated = True
        logger.info(f"攝影機 {self.camera_id} 校準完成！")
    
    def transform(self, raw_x: int, raw_y: int) -> Tuple[float, float]:
        """轉換座標：攝影機座標 → 正規化座標（中心為原點）"""
        if not self.is_calibrated:
            # 未校準時使用簡單轉換
            screen_x = (raw_x - 320) / 320.0
            screen_y = (240 - raw_y) / 240.0
            return screen_x, screen_y
        
        point = np.array([[[raw_x, raw_y]]], dtype=np.float32)
        transformed = cv2.perspectiveTransform(point, self.homography_matrix)
        
        screen_x = float(transformed[0][0][0])
        screen_y = float(transformed[0][0][1])
        
        # 限制範圍
        screen_x = max(-1.0, min(1.0, screen_x))
        screen_y = max(-1.0, min(1.0, screen_y))
        
        return screen_x, screen_y
    
    def save(self, filepath: str):
        """儲存校準資料"""
        data = {
            "camera_id": self.camera_id,
            "src_points": self.src_points,
            "dst_points": self.dst_points,
            "homography": self.homography_matrix.tolist() if self.homography_matrix is not None else None,
            "is_calibrated": self.is_calibrated
        }
        with open(filepath, 'w') as f:
            json.dump(data, f, indent=2)
        logger.info(f"校準資料已儲存至 {filepath}")
    
    def load(self, filepath: str) -> bool:
        """載入校準資料"""
        try:
            with open(filepath, 'r') as f:
                data = json.load(f)
            
            self.src_points = data["src_points"]
            self.dst_points = data["dst_points"]
            if data["homography"]:
                self.homography_matrix = np.array(data["homography"])
                self.is_calibrated = True
            logger.info(f"已載入攝影機 {self.camera_id} 的校準資料")
            return True
        except FileNotFoundError:
            logger.warning(f"找不到校準檔案 {filepath}")
            return False
        except Exception as e:
            logger.error(f"載入校準資料失敗: {e}")
            return False
    
    def reset(self):
        """重置校準"""
        self.src_points = []
        self.homography_matrix = None
        self.is_calibrated = False
        logger.info(f"攝影機 {self.camera_id} 校準已重置")

# ============== IR 偵測器 ==============

class IRDetector:
    """IR 光點偵測器"""
    
    def __init__(self, config: DetectionConfig):
        self.config = config
    
    def detect(self, frame: np.ndarray) -> List[Tuple[int, int, int]]:
        """
        偵測 IR 光點
        返回: [(x, y, area), ...] 光點列表
        """
        # 如果是彩色影像，轉為灰階
        if len(frame.shape) == 3:
            gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)
        else:
            gray = frame
        
        # 二值化
        _, thresh = cv2.threshold(gray, self.config.threshold, 255, cv2.THRESH_BINARY)
        
        # 找輪廓
        contours, _ = cv2.findContours(thresh, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)
        
        points = []
        for contour in contours:
            area = cv2.contourArea(contour)
            
            # 過濾面積
            if area < self.config.min_area or area > self.config.max_area:
                continue
            
            # 計算中心點
            M = cv2.moments(contour)
            if M["m00"] == 0:
                continue
            
            cx = int(M["m10"] / M["m00"])
            cy = int(M["m01"] / M["m00"])
            
            points.append((cx, cy, int(area)))
        
        return points

# ============== 分區分類器 ==============

class ZoneClassifier:
    """分區分類器：判定射手編號"""
    
    def __init__(self, camera_width: int = 640):
        self.camera_width = camera_width
        self.zone_width = camera_width // 3
    
    def classify(self, camera_id: str, raw_x: int) -> int:
        """
        根據 X 座標判定射手編號
        
        攝影機 A: 射手 1, 2, 3
        攝影機 B: 射手 4, 5, 6
        """
        zone = raw_x // self.zone_width  # 0, 1, 2
        zone = min(zone, 2)  # 確保不超過 2
        
        if camera_id == "A":
            return zone + 1  # 1, 2, 3
        else:  # "B"
            return zone + 4  # 4, 5, 6

# ============== 音效播放器 ==============

class SoundPlayer:
    """音效播放器"""
    
    def __init__(self):
        self.enabled = True
        self._init_audio()
    
    def _init_audio(self):
        """初始化音效系統"""
        try:
            import pygame
            pygame.mixer.init()
            self.pygame = pygame
            self.sounds: Dict[str, any] = {}
            logger.info("音效系統初始化成功 (pygame)")
        except ImportError:
            logger.warning("pygame 未安裝，音效功能停用")
            self.pygame = None
    
    def load_sound(self, name: str, filepath: str):
        """載入音效檔案"""
        if self.pygame is None:
            return
        
        try:
            self.sounds[name] = self.pygame.mixer.Sound(filepath)
            logger.info(f"已載入音效: {name}")
        except Exception as e:
            logger.error(f"載入音效失敗 {filepath}: {e}")
    
    def play(self, name: str):
        """播放音效"""
        if not self.enabled or self.pygame is None:
            return
        
        if name in self.sounds:
            self.sounds[name].play()

# ============== TCP 伺服器 ==============

# ============== 資料庫與通訊系統 ==============

class ResultDatabase:
    """射擊成績資料庫管理器"""
    def __init__(self, db_path: str = "training_results.db"):
        self.db_path = db_path
        self._init_db()

    def _init_db(self):
        """初始化資料庫表格"""
        try:
            with sqlite3.connect(self.db_path) as conn:
                cursor = conn.cursor()
                cursor.execute('''
                    CREATE TABLE IF NOT EXISTS training_results (
                        id INTEGER PRIMARY KEY AUTOINCREMENT,
                        training_date TEXT,
                        training_time TEXT,
                        batch TEXT,
                        company TEXT,
                        squad TEXT,
                        shooter_name TEXT,
                        shooting_mode TEXT,
                        score INTEGER,
                        created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
                    )
                ''')
                conn.commit()
            logger.info(f"資料庫初始化完成: {self.db_path}")
        except Exception as e:
            logger.error(f"資料庫初始化失敗: {e}")

    def save_session_results(self, data: dict):
        """將一整個梯次的成績存入資料庫"""
        try:
            session = data.get('session', {})
            results = data.get('results', [])
            
            now = datetime.now()
            date_str = now.strftime("%Y-%m-%d")
            time_str = now.strftime("%H:%M:%S")
            
            with sqlite3.connect(self.db_path) as conn:
                cursor = conn.cursor()
                for res in results:
                    cursor.execute('''
                        INSERT INTO training_results 
                        (training_date, training_time, batch, company, squad, shooter_name, shooting_mode, score)
                        VALUES (?, ?, ?, ?, ?, ?, ?, ?)
                    ''', (
                        date_str,
                        time_str,
                        session.get('batch', ''),
                        session.get('company', ''),
                        session.get('squad', ''),
                        res.get('name', ''),
                        session.get('mode', ''),
                        res.get('score', 0)
                    ))
                conn.commit()
            logger.info(f"成功儲存 {len(results)} 筆成績資料。")
            return True
        except Exception as e:
            logger.error(f"儲存成績失敗: {e}")
            return False

class CommandHandler(BaseHTTPRequestHandler):
    """處理來自網頁控制台的 HTTP 指令"""
    tracker_instance = None

    def log_message(self, format, *args):
        return # 簡化日誌，不印出每次請求

    def do_OPTIONS(self):
        """處理 CORS 預檢請求"""
        self.send_response(200)
        self.send_header('Access-Control-Allow-Origin', '*')
        self.send_header('Access-Control-Allow-Methods', 'POST, OPTIONS')
        self.send_header('Access-Control-Allow-Headers', 'Content-Type')
        self.end_headers()

    def do_POST(self):
        """處理指令發送"""
        if self.path == '/command':
            content_length = int(self.headers['Content-Length'])
            post_data = self.rfile.read(content_length)
            
            try:
                command_data = json.loads(post_data.decode('utf-8'))
                action = command_data.get('action')
                data = command_data.get('data', {})
                
                logger.info(f"收到網頁指令: {action}")
                
                success = True
                message = "Command processed"
                
                # 處理特定的指令
                if action == 'save_results':
                    success = self.tracker_instance.db.save_session_results(data)
                elif action == 'update_threshold':
                    new_threshold = data.get('threshold', 200)
                    self.tracker_instance.detector.config.threshold = new_threshold
                    logger.info(f"偵測閾值已更新為: {new_threshold}")
                elif action == 'knockdown_all':
                    # 連動到實體功能 (待實作)
                    pass
                
                self.send_response(200)
                self.send_header('Content-Type', 'application/json')
                self.send_header('Access-Control-Allow-Origin', '*')
                self.end_headers()
                self.wfile.write(json.dumps({"status": "success" if success else "error"}).encode())
                
            except Exception as e:
                self.send_response(500)
                self.end_headers()
                logger.error(f"指令處裡錯誤: {e}")

class CommandHTTPServer:
    """HTTP 指令伺服器封裝"""
    def __init__(self, host: str, port: int, tracker):
        self.host = host
        self.port = port
        CommandHandler.tracker_instance = tracker
        self.server = None
        self.thread = None

    def start(self):
        self.server = HTTPServer((self.host, self.port), CommandHandler)
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()
        logger.info(f"HTTP 指令伺服器啟動於 http://{self.host}:{self.port}")

    def stop(self):
        if self.server:
            self.server.shutdown()
            logger.info("HTTP 指令伺服器已停止")

class TCPServer:
    """TCP 伺服器：傳送座標資料"""
    
    def __init__(self, host: str, port: int, name: str):
        self.host = host
        self.port = port
        self.name = name
        self.server_socket: Optional[socket.socket] = None
        self.clients: List[socket.socket] = []
        self.running = False
        self.message_queue: queue.Queue = queue.Queue()
        
        self._accept_thread: Optional[threading.Thread] = None
        self._send_thread: Optional[threading.Thread] = None
    
    def start(self):
        """啟動伺服器"""
        self.server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.server_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.server_socket.bind((self.host, self.port))
        self.server_socket.listen(5)
        self.server_socket.settimeout(1.0)
        
        self.running = True
        
        self._accept_thread = threading.Thread(target=self._accept_loop, daemon=True)
        self._accept_thread.start()
        
        self._send_thread = threading.Thread(target=self._send_loop, daemon=True)
        self._send_thread.start()
        
        logger.info(f"TCP 伺服器 {self.name} 啟動於 {self.host}:{self.port}")
    
    def stop(self):
        """停止伺服器"""
        self.running = False
        
        for client in self.clients:
            try:
                client.close()
            except:
                pass
        self.clients.clear()
        
        if self.server_socket:
            self.server_socket.close()
        
        logger.info(f"TCP 伺服器 {self.name} 已停止")
    
    def _accept_loop(self):
        """接受連接的迴圈"""
        while self.running:
            try:
                client_socket, addr = self.server_socket.accept()
                self.clients.append(client_socket)
                logger.info(f"{self.name}: 新連接來自 {addr}")
            except socket.timeout:
                continue
            except Exception as e:
                if self.running:
                    logger.error(f"{self.name} 接受連接錯誤: {e}")
    
    def _send_loop(self):
        """發送訊息的迴圈"""
        while self.running:
            try:
                message = self.message_queue.get(timeout=0.1)
                self._broadcast(message)
            except queue.Empty:
                continue
    
    def _broadcast(self, message: str):
        """廣播訊息給所有客戶端"""
        data = (message + "\n").encode('utf-8')
        disconnected = []
        
        for client in self.clients:
            try:
                client.sendall(data)
            except Exception as e:
                logger.warning(f"{self.name}: 客戶端斷線")
                disconnected.append(client)
        
        for client in disconnected:
            self.clients.remove(client)
            try:
                client.close()
            except:
                pass
    
    def send(self, data: dict):
        """發送資料（非阻塞）"""
        message = json.dumps(data)
        self.message_queue.put(message)
    
    @property
    def client_count(self) -> int:
        return len(self.clients)

# ============== Basler 攝影機管理器 ==============

class BaslerCameraManager:
    """Basler 攝影機管理器"""
    
    def __init__(self, camera_config: CameraConfig):
        self.config = camera_config
        self.camera = None
        self.is_connected = False
    
    def connect(self) -> bool:
        """連接攝影機"""
        try:
            from pypylon import pylon
            
            # 取得設備工廠
            tl_factory = pylon.TlFactory.GetInstance()
            devices = tl_factory.EnumerateDevices()
            
            if len(devices) == 0:
                logger.error(f"攝影機 {self.config.id}: 找不到 Basler 攝影機")
                return False
            
            # 選擇攝影機
            if self.config.serial_number:
                # 依序號選擇
                for device in devices:
                    if device.GetSerialNumber() == self.config.serial_number:
                        self.camera = pylon.InstantCamera(tl_factory.CreateDevice(device))
                        break
                if self.camera is None:
                    logger.error(f"攝影機 {self.config.id}: 找不到序號 {self.config.serial_number}")
                    return False
            else:
                # 選擇第一台可用的
                self.camera = pylon.InstantCamera(tl_factory.CreateFirstDevice())
            
            # 開啟攝影機
            self.camera.Open()
            
            # 設定參數
            self.camera.Width.Value = self.config.width
            self.camera.Height.Value = self.config.height
            
            if hasattr(self.camera, 'ExposureTime'):
                self.camera.ExposureTime.Value = self.config.exposure_time
            elif hasattr(self.camera, 'ExposureTimeAbs'):
                self.camera.ExposureTimeAbs.Value = self.config.exposure_time
            
            if hasattr(self.camera, 'Gain'):
                self.camera.Gain.Value = self.config.gain
            elif hasattr(self.camera, 'GainRaw'):
                self.camera.GainRaw.Value = int(self.config.gain)
            
            # 開始擷取
            self.camera.StartGrabbing(pylon.GrabStrategy_LatestImageOnly)
            
            self.is_connected = True
            logger.info(f"攝影機 {self.config.id}: 連接成功")
            return True
            
        except ImportError:
            logger.error("pypylon 未安裝，請執行: pip install pypylon")
            return False
        except Exception as e:
            logger.error(f"攝影機 {self.config.id} 連接失敗: {e}")
            return False
    
    def read(self) -> Tuple[bool, Optional[np.ndarray]]:
        """讀取一幀影像"""
        if not self.is_connected or self.camera is None:
            return False, None
        
        try:
            from pypylon import pylon
            
            grab_result = self.camera.RetrieveResult(5000, pylon.TimeoutHandling_ThrowException)
            
            if grab_result.GrabSucceeded():
                frame = grab_result.Array
                grab_result.Release()
                return True, frame
            else:
                grab_result.Release()
                return False, None
                
        except Exception as e:
            logger.error(f"攝影機 {self.config.id} 讀取失敗: {e}")
            return False, None
    
    def disconnect(self):
        """斷開連接"""
        if self.camera is not None:
            try:
                self.camera.StopGrabbing()
                self.camera.Close()
            except:
                pass
        self.is_connected = False
        logger.info(f"攝影機 {self.config.id}: 已斷開")

# ============== 模擬攝影機（測試用）==============

class MockCameraManager:
    """模擬攝影機（用於測試）"""
    
    def __init__(self, camera_config: CameraConfig):
        self.config = camera_config
        self.is_connected = False
        self.cap = None
    
    def connect(self) -> bool:
        """使用 USB 攝影機或生成測試影像"""
        try:
            # 嘗試使用 USB 攝影機
            camera_index = 0 if self.config.id == "A" else 1
            self.cap = cv2.VideoCapture(camera_index)
            
            if self.cap.isOpened():
                self.cap.set(cv2.CAP_PROP_FRAME_WIDTH, self.config.width)
                self.cap.set(cv2.CAP_PROP_FRAME_HEIGHT, self.config.height)
                self.is_connected = True
                logger.info(f"模擬攝影機 {self.config.id}: 使用 USB 攝影機 {camera_index}")
                return True
        except:
            pass
        
        # 無攝影機，使用純模擬模式
        self.cap = None
        self.is_connected = True
        logger.info(f"模擬攝影機 {self.config.id}: 純模擬模式")
        return True
    
    def read(self) -> Tuple[bool, Optional[np.ndarray]]:
        if self.cap is not None:
            return self.cap.read()
        else:
            # 生成空白影像
            frame = np.zeros((self.config.height, self.config.width), dtype=np.uint8)
            return True, frame
    
    def disconnect(self):
        if self.cap is not None:
            self.cap.release()
        self.is_connected = False

# ============== 主追蹤器 ==============

class IRTracker:
    """IR 追蹤主程式"""
    
    def __init__(self, use_mock: bool = False):
        self.use_mock = use_mock
        
        # 攝影機
        if use_mock:
            self.camera_a = MockCameraManager(config.camera_a)
            self.camera_b = MockCameraManager(config.camera_b)
        else:
            self.camera_a = BaslerCameraManager(config.camera_a)
            self.camera_b = BaslerCameraManager(config.camera_b)
        
        # 校準系統
        self.calibration_a = CalibrationSystem("A")
        self.calibration_b = CalibrationSystem("B")
        
        # 偵測器
        self.detector = IRDetector(config.detection)
        
        # 分區分類
        self.classifier = ZoneClassifier()
        
        # 音效
        self.sound_player = SoundPlayer()
        
        # TCP 伺服器
        self.tcp_ue5 = TCPServer(
            config.network.tcp_host,
            config.network.tcp_port_ue5,
            "UE5"
        )
        self.tcp_web_control = TCPServer(
            config.network.tcp_host,
            config.network.tcp_port_web_control,
            "WebControl"
        )
        self.tcp_web_score = TCPServer(
            config.network.tcp_host,
            config.network.tcp_port_web_score,
            "WebScore"
        )
        
        # HTTP 指令伺服器 (供網頁控制台發送指令)
        self.http_server = CommandHTTPServer(
            config.network.tcp_host,
            8080, # 預設 HTTP Port
            self
        )
        
        # 資料庫
        self.db = ResultDatabase()
        
        # 狀態
        self.running = False
        self.calibration_mode = False
        self.calibrating_camera: Optional[str] = None
        
        # 統計
        self.shot_count = 0
        self.fps_counter = 0
        self.fps = 0
        self.last_fps_time = time.time()
    
    def initialize(self) -> bool:
        """初始化系統"""
        logger.info("=" * 50)
        logger.info("T91 紅外線射擊訓練系統")
        logger.info("=" * 50)
        
        # 連接攝影機
        logger.info("正在連接攝影機...")
        if not self.camera_a.connect():
            logger.error("攝影機 A 連接失敗")
            return False
        
        if not self.camera_b.connect():
            logger.error("攝影機 B 連接失敗")
            return False
        
        # 載入校準
        self.calibration_a.load(config.calibration_file_a)
        self.calibration_b.load(config.calibration_file_b)
        
        # 載入音效
        self.sound_player.load_sound("gunshot", config.gunshot_sound)
        
        # 啟動 TCP 伺服器
        self.tcp_ue5.start()
        self.tcp_web_control.start()
        self.tcp_web_score.start()
        
        # 啟動 HTTP 伺服器
        self.http_server.start()
        
        logger.info("系統初始化完成")
        return True
    
    def process_camera(self, camera, calibration: CalibrationSystem, camera_id: str) -> List[dict]:
        """處理單一攝影機"""
        ret, frame = camera.read()
        if not ret or frame is None:
            return []
        
        # 偵測光點
        points = self.detector.detect(frame)
        
        shots = []
        for raw_x, raw_y, area in points:
            # 校準模式
            if self.calibration_mode and self.calibrating_camera == camera_id:
                count = calibration.add_calibration_point(raw_x, raw_y)
                if count >= 4:
                    self.calibration_mode = False
                    self.calibrating_camera = None
                    calibration.save(
                        config.calibration_file_a if camera_id == "A" else config.calibration_file_b
                    )
                continue
            
            # 座標轉換
            screen_x, screen_y = calibration.transform(raw_x, raw_y)
            
            # 轉為像素座標（1920×1200）
            pixel_x = int((screen_x + 1.0) * 960)
            pixel_y = int((1.0 - screen_y) * 600)
            
            # 分區判定
            shooter_id = self.classifier.classify(camera_id, raw_x)
            
            # 建立射擊資料
            shot_data = {
                "type": "shot",
                "timestamp": time.time(),
                "camera_id": camera_id,
                "shooter_id": shooter_id,
                "raw_x": raw_x,
                "raw_y": raw_y,
                "screen_x": round(screen_x, 4),
                "screen_y": round(screen_y, 4),
                "screen_pixel_x": pixel_x,
                "screen_pixel_y": pixel_y
            }
            
            shots.append(shot_data)
            
            # 播放槍聲
            self.sound_player.play("gunshot")
            
            # 統計
            self.shot_count += 1
        
        return shots
    
    def run(self):
        """主迴圈"""
        self.running = True
        logger.info("開始追蹤...")
        logger.info("按鍵說明:")
        logger.info("  C - 校準攝影機 A")
        logger.info("  V - 校準攝影機 B")
        logger.info("  R - 重置所有校準")
        logger.info("  Q - 退出")
        
        while self.running:
            # 處理攝影機 A
            shots_a = self.process_camera(self.camera_a, self.calibration_a, "A")
            
            # 處理攝影機 B
            shots_b = self.process_camera(self.camera_b, self.calibration_b, "B")
            
            # 傳送資料
            for shot in shots_a + shots_b:
                self.tcp_ue5.send(shot)
                self.tcp_web_control.send(shot)
                self.tcp_web_score.send(shot)
                
                logger.debug(f"射擊: 射手{shot['shooter_id']} @ ({shot['screen_x']:.2f}, {shot['screen_y']:.2f})")
            
            # 更新 FPS
            self._update_fps()
            
            # 顯示監控視窗（如果有 GUI）
            if self._show_monitor():
                break  # 按下 Q 退出
        
        self.cleanup()
    
    def _update_fps(self):
        """更新 FPS 計算"""
        self.fps_counter += 1
        current_time = time.time()
        elapsed = current_time - self.last_fps_time
        
        if elapsed >= 1.0:
            self.fps = self.fps_counter / elapsed
            self.fps_counter = 0
            self.last_fps_time = current_time
    
    def _show_monitor(self) -> bool:
        """顯示監控視窗，返回 True 表示要退出"""
        # 讀取影像用於顯示
        ret_a, frame_a = self.camera_a.read()
        ret_b, frame_b = self.camera_b.read()
        
        if ret_a and frame_a is not None:
            # 轉為彩色顯示
            if len(frame_a.shape) == 2:
                display_a = cv2.cvtColor(frame_a, cv2.COLOR_GRAY2BGR)
            else:
                display_a = frame_a.copy()
            
            # 繪製分區線
            h, w = display_a.shape[:2]
            cv2.line(display_a, (w//3, 0), (w//3, h), (0, 255, 0), 1)
            cv2.line(display_a, (2*w//3, 0), (2*w//3, h), (0, 255, 0), 1)
            
            # 繪製區域標籤
            cv2.putText(display_a, "1", (w//6, 30), cv2.FONT_HERSHEY_SIMPLEX, 1, (0, 255, 0), 2)
            cv2.putText(display_a, "2", (w//2, 30), cv2.FONT_HERSHEY_SIMPLEX, 1, (0, 255, 0), 2)
            cv2.putText(display_a, "3", (5*w//6, 30), cv2.FONT_HERSHEY_SIMPLEX, 1, (0, 255, 0), 2)
            
            # 顯示狀態
            status = "CALIBRATING A" if self.calibrating_camera == "A" else f"FPS: {self.fps:.1f}"
            cv2.putText(display_a, status, (10, h-10), cv2.FONT_HERSHEY_SIMPLEX, 0.5, (255, 255, 255), 1)
            
            cv2.imshow("Camera A", display_a)
        
        if ret_b and frame_b is not None:
            if len(frame_b.shape) == 2:
                display_b = cv2.cvtColor(frame_b, cv2.COLOR_GRAY2BGR)
            else:
                display_b = frame_b.copy()
            
            h, w = display_b.shape[:2]
            cv2.line(display_b, (w//3, 0), (w//3, h), (0, 255, 0), 1)
            cv2.line(display_b, (2*w//3, 0), (2*w//3, h), (0, 255, 0), 1)
            
            cv2.putText(display_b, "4", (w//6, 30), cv2.FONT_HERSHEY_SIMPLEX, 1, (0, 255, 0), 2)
            cv2.putText(display_b, "5", (w//2, 30), cv2.FONT_HERSHEY_SIMPLEX, 1, (0, 255, 0), 2)
            cv2.putText(display_b, "6", (5*w//6, 30), cv2.FONT_HERSHEY_SIMPLEX, 1, (0, 255, 0), 2)
            
            status = "CALIBRATING B" if self.calibrating_camera == "B" else f"Shots: {self.shot_count}"
            cv2.putText(display_b, status, (10, h-10), cv2.FONT_HERSHEY_SIMPLEX, 0.5, (255, 255, 255), 1)
            
            cv2.imshow("Camera B", display_b)
        
        # 鍵盤處理
        key = cv2.waitKey(1) & 0xFF
        
        if key == ord('q'):
            return True
        elif key == ord('c'):
            self.start_calibration("A")
        elif key == ord('v'):
            self.start_calibration("B")
        elif key == ord('r'):
            self.calibration_a.reset()
            self.calibration_b.reset()
        
        return False
    
    def start_calibration(self, camera_id: str):
        """開始校準"""
        self.calibration_mode = True
        self.calibrating_camera = camera_id
        
        if camera_id == "A":
            self.calibration_a.reset()
        else:
            self.calibration_b.reset()
        
        logger.info(f"開始校準攝影機 {camera_id}")
        logger.info("請依序對準投影幕的: 左上 → 右上 → 右下 → 左下")
    
    def cleanup(self):
        """清理資源"""
        self.running = False
        
        self.camera_a.disconnect()
        self.camera_b.disconnect()
        
        self.tcp_ue5.stop()
        self.tcp_web_control.stop()
        self.tcp_web_score.stop()
        
        self.http_server.stop()
        
        cv2.destroyAllWindows()
        
        logger.info("系統已關閉")

# ============== 主程式 ==============

def main():
    import argparse
    
    parser = argparse.ArgumentParser(description='T91 IR 射擊追蹤系統')
    parser.add_argument('--mock', action='store_true', help='使用模擬攝影機（測試用）')
    parser.add_argument('--threshold', type=int, default=200, help='IR 偵測閾值 (0-255)')
    args = parser.parse_args()
    
    # 套用參數
    config.detection.threshold = args.threshold
    
    # 建立追蹤器
    tracker = IRTracker(use_mock=args.mock)
    
    # 初始化
    if not tracker.initialize():
        logger.error("初始化失敗")
        return
    
    # 執行
    try:
        tracker.run()
    except KeyboardInterrupt:
        logger.info("使用者中斷")
    finally:
        tracker.cleanup()

if __name__ == "__main__":
    main()
