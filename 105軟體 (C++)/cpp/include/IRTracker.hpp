#pragma once

#include <atomic>
#include <iostream>
#include <json.hpp>
#include <memory>
#include <mutex>
#include <opencv2/calib3d.hpp>
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <string>
#include <thread>
#include <vector>

// 新增服務類別（功能 A1 + B4）
#include "CalibrationService.hpp"
#include "HitDetectionService.hpp"

using json = nlohmann::json;

namespace T91 {

struct CameraConfig {
  std::string id;
  std::string serial_number;
  int width = 640;
  int height = 480;
  int fps = 90;
  double exposure_time = 1000.0;
  double gain = 10.0;
};

struct DetectionConfig {
  int threshold = 200;
  int min_area = 3;
  int max_area = 500;
};

struct ShotRecord {
  int shooter_id;
  double x;
  double y;
  long long timestamp;
  long long sequence;
  char camera;   // 'A' 或 'B'，標示來自哪台相機
  int intensity; // 新增：代表雷射點的強弱 (像素面積)

  // 功能 A1：新增命中判定詳細資訊
  double confidence = 0.0;           // 命中信心度 [0, 1]
  bool is_boundary_hit = false;      // 是否邊界命中
  double distance_from_center = 0.0; // 距離目標中心
};

class IRTracker {
public:
  IRTracker();
  ~IRTracker();

  bool initialize();
  void start();
  void stop();

  // 指令介面
  void updateThreshold(int value);
  void resetHits();
  void startCalibration(); // 手動校準（舊方式）

  // 功能 B4：新增自動校準介面
  void startAutoCalibrationA();
  void startAutoCalibrationB();
  json getCalibrationStatus();

  // 引導式校準 (多點雷射校準)
  void startGuidedCalibration(char camera);
  void stopGuidedCalibration();
  void setGuidedDisplay(float x, float y);
  json confirmGuidedPoint(char camera, float rawX, float rawY, float corrX, float corrY);
  json undoGuidedPoint(char camera);
  json computeGuidedCalibration(char camera);
  void saveGuidedCalibration(char camera);

  void startScoring() {
    scoring_enabled_ = true;
    std::cout << "[SCORING] START Command Received" << std::endl;
  }
  void stopScoring() {
    scoring_enabled_ = false;
    std::cout << "[SCORING] STOP Command Received" << std::endl;
  }
  void saveResults(const json &data);
  void setShootingMode(const std::string &mode) {
    shooting_mode_ = mode;
    // 更新距離比例因子
    distance_scale_factor_ = HitDetectionService::getScaleFactorForMode(mode);
    hit_detection_svc_->setShootingMode(mode); // 功能 A1：同步到服務
    std::cout << "[MODE] Switched to: " << mode
              << " (scaleFactor: " << distance_scale_factor_ << ")"
              << std::endl;
  }
  void setTimeOfDay(const std::string &time) {
    time_of_day_ = time;
    std::cout << "[TIME] Switched to: " << time << std::endl;
  }
  void setWeather(const std::string &weather) {
    weather_ = weather;
    std::cout << "[WEATHER] Switched to: " << weather << std::endl;
  }
  void setTargetAdjustments(double scale, double vertical, double spacing,
                            double edgePadding);
  void setZeroingOffsets(double x, double y) {
    zeroing_x_ = x;
    zeroing_y_ = y;
    hit_detection_svc_->setModeZeroingOffsets(x, y); // 功能 A1：同步到服務
    std::cout << "[ZEROING] Mode Offset X: " << x << " Y: " << y << std::endl;
  }
  void setGlobalZeroingOffsets(double x, double y) {
    global_zeroing_x_ = x;
    global_zeroing_y_ = y;
    hit_detection_svc_->setGlobalZeroingOffsets(x, y); // 功能 A1：同步到服務
    std::cout << "[ZEROING] Global Offset X: " << x << " Y: " << y << std::endl;
  }
  void setShooterZeroing(int id, double x, double y);
  void setShooterBaseZeroing(int id, double x,
                             double y); // 25m 基準偏移（隨距離縮放）
  void setShooterIntensity(int id, int intensity);
  void setShooterMinIntensity(int id, int intensity);
  void setIntensityIdEnabled(bool enabled) { use_intensity_id_ = enabled; }
  void knockdownAll();
  void resetAll();
  void knockdownTarget(int shooterId); // 個別倒靶
  void resetTarget(int shooterId);     // 個別起靶
  bool isTargetDown(int shooterId);    // 查詢靶位狀態
  void registerHit(int shooterId);     // 前端通知命中

  // 助手函數：檢查點位是否擊中指定的射手靶板
  bool isPointInTarget(float x, float y, int shooter_id);

  // 數據獲取
  json getShots();
  json getState();

  static void log(const std::string &msg); // 新增：靜態日誌方法

private:
  void processingLoop();

  std::atomic<bool> running_{false};
  std::thread processing_thread_;

  CameraConfig config_a_, config_b_;
  DetectionConfig detection_config_;

  std::mutex shots_mutex_;
  std::mutex calib_mutex_;
  std::mutex status_mutex_; // 新增：保護 hits_ 與 targets_down_
  std::vector<ShotRecord> latest_shots_;

  int hits_[6] = {0, 0, 0, 0, 0, 0};
  bool targets_down_[6] = {false, false, false, false, false, false};

  std::string shooting_mode_ = "25";
  std::string weather_ = "sunny";
  std::string time_of_day_ = "day";

  // 靶板調整參數
  double target_scale_ = 1.0;
  double target_vertical_ = 0.0;
  double target_spacing_ = 1.0;
  double target_edge_padding_ = 1.0;

  // 彈著修正 (歸零) 偏移量
  double zeroing_x_ = 0.0; // 模式專用偏移
  double zeroing_y_ = 0.0;
  double global_zeroing_x_ = 0.0; // 全域系統偏移
  double global_zeroing_y_ = 0.0;

  // 射手/槍枝個別參數
  double shooter_zeroing_x_[6] = {0, 0, 0, 0, 0, 0}; // 手動微調（不縮放）
  double shooter_zeroing_y_[6] = {0, 0, 0, 0, 0, 0};
  double shooter_base_zeroing_x_[6] = {0, 0, 0,
                                       0, 0, 0}; // 25m 基準偏移（隨距離縮放）
  double shooter_base_zeroing_y_[6] = {0, 0, 0, 0, 0, 0};
  double distance_scale_factor_ = 1.0; // 距離比例因子
  int shooter_target_intensity_[6] = {0, 0, 0, 0, 0, 0};
  int shooter_min_intensity_[6] = {0, 0, 0, 0, 0, 0};
  bool use_intensity_id_ = false;

  std::atomic<long long> shot_seq_{0};
  std::atomic<bool> is_calibrating_{false};
  std::atomic<bool> scoring_enabled_{false};
  std::chrono::steady_clock::time_point last_calib_time_;
  std::chrono::steady_clock::time_point
      last_shot_time_[6]; // 每位射手的上次擊發時間
  std::vector<cv::Point2f> calib_points_a_, calib_points_b_;
  std::vector<cv::Point2f> avg_buffer_a_,
      avg_buffer_b_;  // 新增：用於平均校正點的緩衝區
  int avg_limit_ = 15; // 平均 15 幀以提升精度（原 5 幀不足以消除手持抖動）
  double calib_reproj_error_a_ = -1.0;
  double calib_reproj_error_b_ = -1.0;

  // 引導式校準狀態
  std::atomic<bool> guided_calib_active_{false};
  char guided_calib_camera_ = 'A';
  std::mutex guided_mutex_;
  cv::Point2f guided_raw_avg_{0, 0};
  cv::Point2f guided_estimated_{0, 0};
  cv::Point2f guided_display_{0, 0};
  std::atomic<bool> guided_has_detection_{false};
  std::chrono::steady_clock::time_point guided_detection_time_;
  std::vector<cv::Point2f> guided_raw_buffer_;
  std::vector<std::pair<cv::Point2f, cv::Point2f>> guided_pairs_a_;
  std::vector<std::pair<cv::Point2f, cv::Point2f>> guided_pairs_b_;
  cv::Mat guided_homography_a_, guided_homography_b_; // 計算結果（尚未儲存）
  
  // 校準目標座標：修正為 (5%, 95%) 實際位置，避免 10% 邊緣偏差
  // 物理尺寸: Camera A: 390×225cm @ 430cm, Camera B: 402×225cm @ 440cm
  const std::vector<cv::Point2f> dst_points_ = {
      {-0.9f,  0.9f}, // 左上 (5%, 5%)
      { 0.9f,  0.9f}, // 右上 (95%, 5%)
      { 0.9f, -0.9f}, // 右下 (95%, 95%)
      {-0.9f, -0.9f}  // 左下 (5%, 95%)
  };

  std::unique_ptr<class IRTrackerImpl> impl_;

  // 功能 A1 + B4：新增服務類別
  std::unique_ptr<HitDetectionService> hit_detection_svc_;
  std::unique_ptr<CalibrationService> calibration_svc_;
};

} // namespace T91
