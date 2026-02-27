#include "IRTracker.hpp"
#include "CameraManager.hpp"
#include "Detector.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <windows.h>

namespace T91 {

class IRTrackerImpl {
public:
  Detector detector;
  cv::Mat homography_a, homography_b;
  bool calibrated_a = false, calibrated_b = false;
  std::unique_ptr<ICamera> cam_a, cam_b;

  IRTrackerImpl() {
#ifdef USE_PYLON
    cam_a = std::make_unique<BaslerCamera>(23058324);
    cam_b = std::make_unique<BaslerCamera>(23058325);
#else
    cam_a = std::make_unique<MockCamera>(0);
    cam_b = std::make_unique<MockCamera>(1);
#endif
  }

  void loadCalibration(const std::string &path_a, const std::string &path_b) {
    auto load_single = [](const std::string &path, cv::Mat &h, bool &flag) {
      try {
        std::ifstream f(path);
        if (!f.is_open()) {
          std::cout << "[INFO] No calibration file found at " << path
                    << ", skipping." << std::endl;
          return;
        }
        json data = json::parse(f);
        if (data.contains("homography") && !data["homography"].is_null()) {
          h = cv::Mat(3, 3, CV_64F);
          for (int i = 0; i < 3; ++i) {
            for (int j = 0; j < 3; ++j) {
              h.at<double>(i, j) = data["homography"][i][j];
            }
          }
          flag = true;
          std::cout << "[SUCCESS] Loaded calibration from " << path
                    << std::endl;
        }
      } catch (const std::exception &e) {
        std::cerr << "[ERROR] Loading calibration " << path << ": " << e.what()
                  << std::endl;
      }
    };
    load_single(path_a, homography_a, calibrated_a);
    load_single(path_b, homography_b, calibrated_b);
  }

  void saveCalibration(const std::string &path, const cv::Mat &h) {
    try {
      json data;
      data["homography"] = json::array();
      for (int i = 0; i < 3; ++i) {
        json row = json::array();
        for (int j = 0; j < 3; ++j) {
          row.push_back(h.at<double>(i, j));
        }
        data["homography"].push_back(row);
      }
      std::ofstream f(path);
      f << data.dump(4);
      std::cout << "Saved calibration to " << path << std::endl;
    } catch (const std::exception &e) {
      std::cerr << "Error saving calibration " << path << ": " << e.what()
                << std::endl;
    }
  }

  cv::Point2f transform(const cv::Point2f &pt, const cv::Mat &h) {
    if (h.empty())
      return pt;
    std::vector<cv::Point2f> src = {pt}, dst;
    cv::perspectiveTransform(src, dst, h);
    return dst[0];
  }
};

IRTracker::IRTracker() : impl_(std::make_unique<IRTrackerImpl>()) {
  config_a_.id = "A";
  config_b_.id = "B";
  auto far_past = std::chrono::steady_clock::now() - std::chrono::hours(1);
  for (int i = 0; i < 6; ++i)
    last_shot_time_[i] = far_past;

  // 功能 A1 + B4：初始化新服務
  hit_detection_svc_ = std::make_unique<HitDetectionService>();
  calibration_svc_ = std::make_unique<CalibrationService>();

  std::cout << "[IRTracker] Services initialized (HitDetection + Calibration)"
            << std::endl;
}

IRTracker::~IRTracker() { stop(); }

bool IRTracker::initialize() {
  std::cout << "Initializing T91 IR Tracker (C++)..." << std::endl;

  // 功能 B4：使用 CalibrationService 載入校準
  calibration_svc_->loadCalibration("calibration_a.json", "calibration_b.json");

  // 同時載入到 impl（向後兼容，未來可移除）
  impl_->loadCalibration("calibration_a.json", "calibration_b.json");

  // 檢查是否跳過相機初始化：
  //   1. 環境變數 T91_NO_CAMERA=1
  //   2. 工作目錄下存在 no_camera.flag 檔案
  bool skip_camera = false;
  const char* no_cam_env = std::getenv("T91_NO_CAMERA");
  if (no_cam_env && std::string(no_cam_env) == "1") {
    skip_camera = true;
    std::cout << "[INFO] T91_NO_CAMERA=1 set. Skipping camera init." << std::endl;
  }
  {
    std::ifstream flag_file("no_camera.flag");
    if (flag_file.good()) {
      skip_camera = true;
      std::cout << "[INFO] no_camera.flag found. Skipping camera init." << std::endl;
    }
  }

  bool a_ok = false, b_ok = false;

  if (!skip_camera) {
    std::cout << "[INFO] Initializing Pylon SDK..." << std::endl;
    if (PylonInitialize() != GENAPI_E_OK) {
      std::cerr << "[WARNING] PylonInitialize failed. Running without cameras." << std::endl;
    } else {
      std::this_thread::sleep_for(std::chrono::milliseconds(2000));

      size_t numDevices = 0;
      PylonEnumerateDevices(&numDevices);
      std::cout << "[INFO] Found " << numDevices
                << " Basler device(s) after initial scan." << std::endl;

      a_ok = impl_->cam_a->open();
      if (!a_ok)
        std::cerr << "[WARNING] Camera A failed to open" << std::endl;
      else
        std::cout << "[INFO] Camera A initialized." << std::endl;

      b_ok = impl_->cam_b->open();
      if (!b_ok)
        std::cerr << "[WARNING] Camera B failed to open" << std::endl;
      else
        std::cout << "[INFO] Camera B initialized." << std::endl;
    }
  }

  if (!a_ok && !b_ok) {
    std::cerr
        << "[WARNING] No cameras. Running in NO-CAMERA mode (HTTP API only)."
        << std::endl;
  } else {
    std::cout << "[INFO] Tracker initialized successfully with "
              << (a_ok ? "Camera A " : "") << (b_ok ? "Camera B" : "")
              << std::endl;
  }
  return true;
}

void IRTracker::start() {
  if (running_)
    return;
  running_ = true;
  processing_thread_ = std::thread(&IRTracker::processingLoop, this);
}

void IRTracker::stop() {
  running_ = false;
  if (processing_thread_.joinable()) {
    processing_thread_.join();
  }
}

void IRTracker::updateThreshold(int value) {
  impl_->detector.setThreshold(value);
  std::cout << "Threshold updated to: " << value << std::endl;
}

void IRTracker::resetHits() {
  std::lock_guard<std::mutex> lock(status_mutex_);
  for (int i = 0; i < 6; ++i)
    hits_[i] = 0;
  std::cout << "Hits reset." << std::endl;
}

void IRTracker::startCalibration() {
  std::cout << "[COMMAND] Starting calibration mode..." << std::endl;
  {
    std::lock_guard<std::mutex> lock(calib_mutex_);
    calib_points_a_.clear();
    calib_points_b_.clear();
  }
  last_calib_time_ = std::chrono::steady_clock::now() - std::chrono::seconds(2);
  is_calibrating_ = true;
}

void IRTracker::saveResults(const json &data) {
  std::cout << "Saving results to backend storage..." << std::endl;
  try {
    // 1. 原有的 JSON 備份 (開發者除錯用)
    std::ofstream f("last_results.json");
    if (f.is_open()) {
      f << data.dump(4);
      f.close();
    }

    // 2. 實體存檔到「射擊成績」資料夾 (使用者觀看用)
    std::string batch = "未命名";
    if (data.contains("session") && data["session"].contains("batch")) {
      batch = data["session"]["batch"].get<std::string>();
      if (batch.empty())
        batch = "未命名";
    }

    std::string company =
        data.contains("session") ? data["session"].value("company", "") : "";
    std::string squad =
        data.contains("session") ? data["session"].value("squad", "") : "";
    std::string mode =
        data.contains("session") ? data["session"].value("mode", "") : "";
    std::string timestamp = data.contains("session")
                                ? data["session"].value("timestamp", "00-00-00")
                                : "00-00-00";

    // 整理檔名 (去除不合法字元)
    std::string safe_name =
        batch + "_" + company + "_" + squad + "_" + timestamp;
    std::replace(safe_name.begin(), safe_name.end(), ':', '-');
    std::replace(safe_name.begin(), safe_name.end(), ' ', '_');
    std::replace(safe_name.begin(), safe_name.end(), 'T', '_');
    std::replace(safe_name.begin(), safe_name.end(), '.', '-');

    if (safe_name.length() > 100)
      safe_name = safe_name.substr(0, 100);

    std::string csvPath = "../射擊成績/成績_" + safe_name + ".csv";
    std::ofstream csv(csvPath);

    if (csv.is_open()) {
      // 寫入 UTF-8 BOM 以利 Excel 正常顯示中文
      unsigned char bom[] = {0xEF, 0xBB, 0xBF};
      csv.write((char *)bom, 3);

      // 寫入標頭與基本資訊
      csv << "梯次,連隊,班別,模式,時間\n";
      csv << batch << "," << company << "," << squad << "," << mode << "m,"
          << timestamp << "\n\n";

      // 寫入成績細節
      csv << "編號,姓名,中彈發數\n";
      if (data.contains("results") && data["results"].is_array()) {
        for (auto &r : data["results"]) {
          csv << r.value("id", 0) << ","
              << (r.contains("name") ? r["name"].get<std::string>() : "無名")
              << "," << r.value("score", 0) << "\n";
        }
      }
      csv.close();
      std::cout << "[SUCCESS] Results archived to: " << csvPath << std::endl;
      T91::IRTracker::log("Results archived to: " + csvPath);
    } else {
      std::cerr << "[ERROR] Could not create file in '射擊成績' folder. Check "
                   "permissions."
                << std::endl;
    }

  } catch (const std::exception &e) {
    std::cerr << "[ERROR] Failed to save results: " << e.what() << std::endl;
    T91::IRTracker::log("ERROR saving results: " + std::string(e.what()));
  }
}

json IRTracker::getShots() {
  std::lock_guard<std::mutex> lock(shots_mutex_);
  json j = json::array();
  for (const auto &shot : latest_shots_) {
    j.push_back({
        {"shooter_id", shot.shooter_id},
        {"x", shot.x},
        {"y", shot.y},
        {"timestamp", shot.timestamp},
        {"sequence", shot.sequence},
        {"camera", std::string(1, shot.camera)}, // 'A' 或 'B'
        {"intensity", shot.intensity}            // 新增：雷射強度
    });
  }
  // 不再清除，以便多個頁面可以同時抓取。由前端過濾重複的時間戳記。
  // latest_shots_.clear();
  return j;
}

json IRTracker::getState() {
  int count_a = 0, count_b = 0;
  {
    std::lock_guard<std::mutex> lock(calib_mutex_);
    count_a = (int)calib_points_a_.size();
    count_b = (int)calib_points_b_.size();
  }

  json hits_json = json::array();
  json targets_json = json::array();
  {
    std::lock_guard<std::mutex> lock(status_mutex_);
    for (int i = 0; i < 6; ++i)
      hits_json.push_back(hits_[i]);
    for (int i = 0; i < 6; ++i)
      targets_json.push_back(targets_down_[i]);
  }

  json shooters_json = json::array();
  for (int i = 0; i < 6; ++i) {
    shooters_json.push_back({{"id", i + 1},
                             {"baseZeroingX", shooter_base_zeroing_x_[i]},
                             {"baseZeroingY", shooter_base_zeroing_y_[i]},
                             {"zeroingX", shooter_zeroing_x_[i]},
                             {"zeroingY", shooter_zeroing_y_[i]},
                             {"intensity", shooter_target_intensity_[i]},
                             {"minIntensity", shooter_min_intensity_[i]}});
  }

  return {{"weather", weather_},
          {"timeOfDay", time_of_day_},
          {"shootingMode", shooting_mode_},
          {"threshold", detection_config_.threshold},
          {"hits", hits_json},
          {"targetsDown", targets_json},
          {"targetAdjustments",
           {{"scale", target_scale_},
            {"vertical", target_vertical_},
            {"spacing", target_spacing_},
            {"edgePadding", target_edge_padding_},
            {"zeroingX", zeroing_x_},
            {"zeroingY", zeroing_y_}}},
          {"globalZeroingX", global_zeroing_x_},
          {"globalZeroingY", global_zeroing_y_},
          {"shooters", shooters_json},
          {"distanceScaleFactor", distance_scale_factor_},
          {"useIntensityId", use_intensity_id_},
          {"isScoring", scoring_enabled_.load()},
          {"calibrationStatus",
           {{"countA", count_a},
            {"countB", count_b},
            {"isCalibrating", is_calibrating_.load()},
            {"reprojErrorA", calib_reproj_error_a_},
            {"reprojErrorB", calib_reproj_error_b_}}}};
}

void IRTracker::setShooterZeroing(int id, double x, double y) {
  if (id >= 1 && id <= 6) {
    shooter_zeroing_x_[id - 1] = x;
    shooter_zeroing_y_[id - 1] = y;

    // 功能 A1：同步到 HitDetectionService
    hit_detection_svc_->setShooterZeroing(id, x, y);

    std::cout << "[ZEROING] Shooter " << id << " Manual Offset X: " << x
              << " Y: " << y << std::endl;
  }
}

void IRTracker::setShooterBaseZeroing(int id, double x, double y) {
  if (id >= 1 && id <= 6) {
    shooter_base_zeroing_x_[id - 1] = x;
    shooter_base_zeroing_y_[id - 1] = y;

    // 同步到 HitDetectionService
    hit_detection_svc_->setShooterBaseZeroing(id, x, y);

    std::cout << "[ZEROING] Shooter " << id << " Base (25m) X: " << x
              << " Y: " << y << " (current scale: " << distance_scale_factor_
              << "x)" << std::endl;
  }
}

void IRTracker::setShooterIntensity(int id, int intensity) {
  if (id >= 1 && id <= 6) {
    shooter_target_intensity_[id - 1] = intensity;
    std::cout << "[INTENSITY] Shooter " << id
              << " Target Intensity: " << intensity << std::endl;
  }
}

void IRTracker::setShooterMinIntensity(int id, int intensity) {
  if (id >= 1 && id <= 6) {
    shooter_min_intensity_[id - 1] = intensity;
    std::cout << "[MIN_INTENSITY] Shooter " << id
              << " Min Threshold: " << intensity << std::endl;
  }
}

// 功能 A1：使用 HitDetectionService 進行命中判定
//
// 原有邏輯已移至 HitDetectionService，此方法現為委派介面
// 符合憲章 Article 3 (單一職責) - IRTracker 僅協調，不含判定邏輯
bool IRTracker::isPointInTarget(float x, float y, int shooter_id) {
  // 委派給 HitDetectionService
  return hit_detection_svc_->isHit(x, y, shooter_id);

  // ⚠️ TECH-DEBT: MIGRATION
  // 原有 35 行判定邏輯已移至 HitDetectionService::evaluateHit()
  // 保留此方法作為向後兼容介面
  // Deadline: 2026-Q2（確認所有呼叫者遷移後可移除）
}

void IRTracker::setTargetAdjustments(double scale, double vertical,
                                     double spacing, double edgePadding) {
  target_scale_ = scale;
  target_vertical_ = vertical;
  target_spacing_ = spacing;
  target_edge_padding_ = edgePadding;

  // 功能 A1：同步到 HitDetectionService
  hit_detection_svc_->setTargetAdjustments(scale, vertical, spacing,
                                           edgePadding);

  std::cout << "[ADJUST] Scale:" << scale << " Vertical:" << vertical
            << " Spacing:" << spacing << " Edge:" << edgePadding << std::endl;
}

void IRTracker::knockdownAll() {
  std::lock_guard<std::mutex> lock(status_mutex_);
  for (int i = 0; i < 6; ++i) {
    targets_down_[i] = true;
  }
  std::cout << "[KNOCKDOWN] All targets knocked down" << std::endl;
}

void IRTracker::resetAll() {
  std::lock_guard<std::mutex> lock(status_mutex_);
  for (int i = 0; i < 6; ++i) {
    targets_down_[i] = false;
  }
  std::cout << "[RESET] All targets standing" << std::endl;
}

void IRTracker::knockdownTarget(int shooterId) {
  if (shooterId >= 1 && shooterId <= 6) {
    std::lock_guard<std::mutex> lock(status_mutex_);
    targets_down_[shooterId - 1] = true;
    std::cout << "[KNOCKDOWN] Target " << shooterId << " knocked down"
              << std::endl;
  }
}

void IRTracker::resetTarget(int shooterId) {
  if (shooterId >= 1 && shooterId <= 6) {
    std::lock_guard<std::mutex> lock(status_mutex_);
    targets_down_[shooterId - 1] = false;
    std::cout << "[RESET] Target " << shooterId << " standing" << std::endl;
  }
}

bool IRTracker::isTargetDown(int shooterId) {
  if (shooterId >= 1 && shooterId <= 6) {
    std::lock_guard<std::mutex> lock(status_mutex_);
    return targets_down_[shooterId - 1];
  }
  return false;
}

// 前端通知命中（由投影幕判斷紅點在靶板內後呼叫）
void IRTracker::registerHit(int shooterId) {
  if (shooterId >= 1 && shooterId <= 6) {
    std::lock_guard<std::mutex> lock(status_mutex_);
    // 只有訓練中且靶板立著時才計數
    if (scoring_enabled_ && !targets_down_[shooterId - 1]) {
      hits_[shooterId - 1]++;
      std::cout << "[HIT] Shooter " << shooterId
                << " hit registered, total: " << hits_[shooterId - 1]
                << std::endl;
    }
  }
}

void IRTracker::processingLoop() {
  cv::Mat frame_a, frame_b;
  while (running_) {
    // 功能 B4：檢查是否在自動校準模式
    auto active_camera = calibration_svc_->getActiveCamera();
    if (active_camera.has_value() &&
        calibration_svc_->getCurrentMode(active_camera.value()) ==
            CalibrationMode::AUTO_CHESSBOARD) {

      // 自動校準模式
      if (active_camera.value() == CameraId::A && impl_->cam_a->grab(frame_a)) {
        if (calibration_svc_->detectAndCalibrateChessboard(CameraId::A,
                                                           frame_a)) {
          // 校準成功，儲存
          calibration_svc_->saveCalibration("calibration_a.json",
                                            "calibration_b.json");

          // 更新 impl（向後兼容）
          impl_->homography_a = calibration_svc_->getHomography(CameraId::A);
          impl_->calibrated_a = true;

          std::cout << "[IRTracker] Camera A auto calibration completed"
                    << std::endl;
        }
      } else if (active_camera.value() == CameraId::B &&
                 impl_->cam_b->grab(frame_b)) {
        if (calibration_svc_->detectAndCalibrateChessboard(CameraId::B,
                                                           frame_b)) {
          // 校準成功，儲存
          calibration_svc_->saveCalibration("calibration_a.json",
                                            "calibration_b.json");

          // 更新 impl（向後兼容）
          impl_->homography_b = calibration_svc_->getHomography(CameraId::B);
          impl_->calibrated_b = true;

          std::cout << "[IRTracker] Camera B auto calibration completed"
                    << std::endl;
        }
      }

      // 自動校準模式下，不執行正常追蹤
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      continue;
    }

    // Camera A
    if (impl_->cam_a->grab(frame_a)) {
      auto points = impl_->detector.detect(frame_a);
      for (const auto &p : points) {
        if (is_calibrating_) {
          auto now = std::chrono::steady_clock::now();
          // 冷卻時間內或已經集滿 4 點就不再收集
          if (std::chrono::duration_cast<std::chrono::milliseconds>(
                  now - last_calib_time_)
                  .count() >= 1500) {
            std::lock_guard<std::mutex> lock(calib_mutex_);
            if (calib_points_a_.size() < 4) {
              avg_buffer_a_.push_back(cv::Point2f((float)p.x, (float)p.y));

              // 當集滿 avg_limit_ 幀時，過濾離群值後取平均
              if (avg_buffer_a_.size() >= static_cast<size_t>(avg_limit_)) {
                // 1. 計算平均值
                cv::Point2f sum(0, 0);
                for (const auto &pt : avg_buffer_a_)
                  sum += pt;
                cv::Point2f mean = sum * (1.0f / (float)avg_buffer_a_.size());

                // 2. 計算標準差
                float sum_sq_x = 0, sum_sq_y = 0;
                for (const auto &pt : avg_buffer_a_) {
                  sum_sq_x += (pt.x - mean.x) * (pt.x - mean.x);
                  sum_sq_y += (pt.y - mean.y) * (pt.y - mean.y);
                }
                float std_x = std::sqrt(sum_sq_x / (float)avg_buffer_a_.size());
                float std_y = std::sqrt(sum_sq_y / (float)avg_buffer_a_.size());

                // 3. 過濾超過 1.5 倍標準差的離群值後重新平均
                cv::Point2f filtered_sum(0, 0);
                int filtered_count = 0;
                for (const auto &pt : avg_buffer_a_) {
                  if (std::abs(pt.x - mean.x) <= 1.5f * std_x + 0.1f &&
                      std::abs(pt.y - mean.y) <= 1.5f * std_y + 0.1f) {
                    filtered_sum += pt;
                    filtered_count++;
                  }
                }
                cv::Point2f avg = (filtered_count > 0)
                    ? filtered_sum * (1.0f / (float)filtered_count)
                    : mean;

                calib_points_a_.push_back(avg);
                avg_buffer_a_.clear();
                last_calib_time_ = now;

                int current_idx = (int)calib_points_a_.size() - 1;
                std::cout << "[CALIB] Camera A point " << (current_idx + 1)
                          << "/4 Averaged: (" << avg.x << ", " << avg.y 
                          << ") std=(" << std_x << "," << std_y 
                          << ") used " << filtered_count << "/" << avg_limit_ << " samples"
                          << std::endl;

                // 回傳一個點給前端顯示（用於視覺確認）
                {
                  std::lock_guard<std::mutex> s_lock(shots_mutex_);
                  long long ms =
                      std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::system_clock::now().time_since_epoch())
                          .count();
                  latest_shots_.push_back(
                      {0, dst_points_[current_idx].x,
                       dst_points_[current_idx].y, ms, ++shot_seq_, 'A',
                       0});
                }

                if (calib_points_a_.size() == 4) {
                  impl_->homography_a =
                      cv::findHomography(calib_points_a_, dst_points_);
                  impl_->calibrated_a = true;
                  impl_->saveCalibration("calibration_a.json",
                                         impl_->homography_a);

                  // 計算重投影誤差
                  std::vector<cv::Point2f> reproj;
                  cv::perspectiveTransform(calib_points_a_, reproj, impl_->homography_a);
                  double total_err = 0;
                  for (size_t i = 0; i < 4; i++) {
                    double dx = reproj[i].x - dst_points_[i].x;
                    double dy = reproj[i].y - dst_points_[i].y;
                    total_err += std::sqrt(dx*dx + dy*dy);
                  }
                  calib_reproj_error_a_ = total_err / 4.0;
                  std::cout << "[CALIB] Camera A reprojection error: " 
                            << calib_reproj_error_a_ << std::endl;
                }
              }
            }
          }
        } else {
          // 強度過濾 (從相機偵測到的原點強度 p.area 檢查)
          cv::Point2f pt_raw = impl_->transform(
              cv::Point2f((float)p.x, (float)p.y), impl_->homography_a);

          // 暫時計算 shooter_id 以檢查該射手的門檻
          int temp_id =
              static_cast<int>(
                  (pt_raw.x + (float)(zeroing_x_ + global_zeroing_x_) + 1.0) /
                  (2.0 / 3.0)) +
              1;
          if (temp_id < 1)
            temp_id = 1;
          if (temp_id > 3)
            temp_id = 3;

          if (p.area < shooter_min_intensity_[temp_id - 1]) {
            continue; // 低於門檻，直接無視
          }

          cv::Point2f pt = pt_raw;

          // 套用全域與模式彈著修正 (歸零)
          pt.x += (float)(zeroing_x_ + global_zeroing_x_);
          pt.y += (float)(zeroing_y_ + global_zeroing_y_);

          // Camera A: 3 個區域，shooter_id = 1, 2, 3
          int shooter_id = static_cast<int>((pt.x + 1.0) / (2.0 / 3.0)) + 1;
          if (shooter_id < 1)
            shooter_id = 1;
          if (shooter_id > 3)
            shooter_id = 3;

          // 若啟動強度辨識，根據強度修正射手 ID
          if (use_intensity_id_) {
            int best_id = -1;
            int min_diff = 10000;
            for (int i = 0; i < 6; ++i) {
              if (shooter_target_intensity_[i] > 0) {
                int diff = std::abs(p.area - shooter_target_intensity_[i]);
                if (diff < min_diff && diff < shooter_target_intensity_[i] *
                                                  0.5) { // 允許多達 50% 誤差
                  min_diff = diff;
                  best_id = i + 1;
                }
              }
            }
            if (best_id != -1)
              shooter_id = best_id;
          }

          // 套用射手個別歸零：基準偏移 × 距離比例 + 手動微調
          pt.x += (float)(shooter_base_zeroing_x_[shooter_id - 1] *
                              distance_scale_factor_ +
                          shooter_zeroing_x_[shooter_id - 1]);
          pt.y += (float)(shooter_base_zeroing_y_[shooter_id - 1] *
                              distance_scale_factor_ +
                          shooter_zeroing_y_[shooter_id - 1]);

          if (std::abs(global_zeroing_x_) > 0.001 ||
              std::abs(global_zeroing_y_) > 0.001 || p.area > 0) {
            static int log_cnt = 0;
            if (log_cnt++ % 10 == 0)
              std::cout << "[DEBUG] Cam A Shot: (" << pt.x << ", " << pt.y
                        << ") Id: " << shooter_id << " Intensity: " << p.area
                        << " Offsets: "
                        << global_zeroing_x_ + zeroing_x_ +
                               shooter_zeroing_x_[shooter_id - 1]
                        << ", "
                        << global_zeroing_y_ + zeroing_y_ +
                               shooter_zeroing_y_[shooter_id - 1]
                        << std::endl;
          }

          auto now = std::chrono::steady_clock::now();
          bool is_duplicate = false;
          {
            std::lock_guard<std::mutex> lock(shots_mutex_);
            if (!latest_shots_.empty()) {
              const auto &last = latest_shots_.back();
              double dx = pt.x - last.x;
              double dy = pt.y - last.y;
              double dist_sq = dx * dx + dy * dy;
              if (std::chrono::duration_cast<std::chrono::milliseconds>(
                      now - last_shot_time_[shooter_id - 1])
                          .count() < 200 &&
                  dist_sq < 0.05) {
                is_duplicate = true;
              }
            }
          }

          if (!is_duplicate &&
              std::chrono::duration_cast<std::chrono::milliseconds>(
                  now - last_shot_time_[shooter_id - 1])
                      .count() >= 300) {
            last_shot_time_[shooter_id - 1] = now;

            // 功能 A1：計算命中詳細資訊
            HitResult hit_result =
                hit_detection_svc_->evaluateHit(pt.x, pt.y, shooter_id);

            // 永遠記錄射擊點（測試模式 + 訓練模式都顯示紅點）
            {
              std::lock_guard<std::mutex> lock(shots_mutex_);
              long long ms =
                  std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::system_clock::now().time_since_epoch())
                      .count();

              ShotRecord shot;
              shot.shooter_id = shooter_id;
              shot.x = pt.x;
              shot.y = pt.y;
              shot.timestamp = ms;
              shot.sequence = ++shot_seq_;
              shot.camera = 'A';
              shot.intensity = p.area;
              shot.confidence = hit_result.confidence;
              shot.is_boundary_hit = hit_result.is_boundary_hit;
              shot.distance_from_center = hit_result.distance_from_center;

              latest_shots_.push_back(shot);
              if (latest_shots_.size() > 50)
                latest_shots_.erase(latest_shots_.begin());
            }
            // 只有訓練模式才計分和播放音效（前端根據 scoring_enabled 狀態決定）
          }
        }
      }
    }

    // Camera B
    if (impl_->cam_b->grab(frame_b)) {
      auto points = impl_->detector.detect(frame_b);
      for (const auto &p : points) {
        if (is_calibrating_) {
          auto now = std::chrono::steady_clock::now();
          if (std::chrono::duration_cast<std::chrono::milliseconds>(
                  now - last_calib_time_)
                  .count() >= 1500) {
            std::lock_guard<std::mutex> lock(calib_mutex_);
            if (calib_points_b_.size() < 4) {
              avg_buffer_b_.push_back(cv::Point2f((float)p.x, (float)p.y));

              if (avg_buffer_b_.size() >= static_cast<size_t>(avg_limit_)) {
                // 1. 計算平均值
                cv::Point2f sum(0, 0);
                for (const auto &pt : avg_buffer_b_)
                  sum += pt;
                cv::Point2f mean = sum * (1.0f / (float)avg_buffer_b_.size());

                // 2. 計算標準差
                float sum_sq_x = 0, sum_sq_y = 0;
                for (const auto &pt : avg_buffer_b_) {
                  sum_sq_x += (pt.x - mean.x) * (pt.x - mean.x);
                  sum_sq_y += (pt.y - mean.y) * (pt.y - mean.y);
                }
                float std_x = std::sqrt(sum_sq_x / (float)avg_buffer_b_.size());
                float std_y = std::sqrt(sum_sq_y / (float)avg_buffer_b_.size());

                // 3. 過濾離群值後重新平均
                cv::Point2f filtered_sum(0, 0);
                int filtered_count = 0;
                for (const auto &pt : avg_buffer_b_) {
                  if (std::abs(pt.x - mean.x) <= 1.5f * std_x + 0.1f &&
                      std::abs(pt.y - mean.y) <= 1.5f * std_y + 0.1f) {
                    filtered_sum += pt;
                    filtered_count++;
                  }
                }
                cv::Point2f avg = (filtered_count > 0)
                    ? filtered_sum * (1.0f / (float)filtered_count)
                    : mean;

                calib_points_b_.push_back(avg);
                avg_buffer_b_.clear();
                last_calib_time_ = now;

                int current_idx = (int)calib_points_b_.size() - 1;
                std::cout << "[CALIB] Camera B point " << (current_idx + 1)
                          << "/4 Averaged: (" << avg.x << ", " << avg.y 
                          << ") std=(" << std_x << "," << std_y 
                          << ") used " << filtered_count << "/" << avg_limit_ << " samples"
                          << std::endl;

                {
                  std::lock_guard<std::mutex> s_lock(shots_mutex_);
                  long long ms =
                      std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::system_clock::now().time_since_epoch())
                          .count();
                  latest_shots_.push_back(
                      {0, dst_points_[current_idx].x,
                       dst_points_[current_idx].y, ms, ++shot_seq_, 'B',
                       0});
                }

                if (calib_points_b_.size() == 4) {
                  impl_->homography_b = cv::findHomography(
                      calib_points_b_,
                      dst_points_);
                  impl_->calibrated_b = true;
                  impl_->saveCalibration("calibration_b.json",
                                         impl_->homography_b);

                  // 計算重投影誤差
                  std::vector<cv::Point2f> reproj;
                  cv::perspectiveTransform(calib_points_b_, reproj, impl_->homography_b);
                  double total_err = 0;
                  for (size_t i = 0; i < 4; i++) {
                    double dx = reproj[i].x - dst_points_[i].x;
                    double dy = reproj[i].y - dst_points_[i].y;
                    total_err += std::sqrt(dx*dx + dy*dy);
                  }
                  calib_reproj_error_b_ = total_err / 4.0;
                  std::cout << "[CALIB] Camera B reprojection error: " 
                            << calib_reproj_error_b_ << std::endl;
                }
              }
            }
          }
        } else {
          // 強度過濾 (相機 B)
          cv::Point2f pt_raw = impl_->transform(
              cv::Point2f((float)p.x, (float)p.y), impl_->homography_b);
          int temp_id =
              static_cast<int>(
                  (pt_raw.x + (float)(zeroing_x_ + global_zeroing_x_) + 1.0) /
                  (2.0 / 3.0)) +
              4;
          if (temp_id < 4)
            temp_id = 4;
          if (temp_id > 6)
            temp_id = 6;

          if (p.area < shooter_min_intensity_[temp_id - 1]) {
            continue; // 低於門檻，直接無視
          }

          cv::Point2f pt = pt_raw;

          // 套用全域與模式彈著修正 (歸零)
          pt.x += (float)(zeroing_x_ + global_zeroing_x_);
          pt.y += (float)(zeroing_y_ + global_zeroing_y_);

          // Camera B: 3 個區域，shooter_id = 4, 5, 6
          int shooter_id = static_cast<int>((pt.x + 1.0) / (2.0 / 3.0)) + 4;
          if (shooter_id < 4)
            shooter_id = 4;
          if (shooter_id > 6)
            shooter_id = 6;

          // 若啟動強度辨識，根據強度修正射手 ID
          if (use_intensity_id_) {
            int best_id = -1;
            int min_diff = 10000;
            for (int i = 0; i < 6; ++i) {
              if (shooter_target_intensity_[i] > 0) {
                int diff = std::abs(p.area - shooter_target_intensity_[i]);
                if (diff < min_diff &&
                    diff < shooter_target_intensity_[i] * 0.5) {
                  min_diff = diff;
                  best_id = i + 1;
                }
              }
            }
            if (best_id != -1)
              shooter_id = best_id;
          }

          // 套用射手個別歸零：基準偏移 × 距離比例 + 手動微調
          pt.x += (float)(shooter_base_zeroing_x_[shooter_id - 1] *
                              distance_scale_factor_ +
                          shooter_zeroing_x_[shooter_id - 1]);
          pt.y += (float)(shooter_base_zeroing_y_[shooter_id - 1] *
                              distance_scale_factor_ +
                          shooter_zeroing_y_[shooter_id - 1]);

          if (std::abs(global_zeroing_x_) > 0.001 ||
              std::abs(global_zeroing_y_) > 0.001 || p.area > 0) {
            static int log_cnt_b = 0;
            if (log_cnt_b++ % 10 == 0)
              std::cout << "[DEBUG] Cam B Shot: (" << pt.x << ", " << pt.y
                        << ") Id: " << shooter_id << " Intensity: " << p.area
                        << " Offsets: "
                        << global_zeroing_x_ + zeroing_x_ +
                               shooter_zeroing_x_[shooter_id - 1]
                        << ", "
                        << global_zeroing_y_ + zeroing_y_ +
                               shooter_zeroing_y_[shooter_id - 1]
                        << std::endl;
          }

          auto now = std::chrono::steady_clock::now();
          bool is_duplicate = false;
          {
            std::lock_guard<std::mutex> lock(shots_mutex_);
            if (!latest_shots_.empty()) {
              const auto &last = latest_shots_.back();
              double dx = pt.x - last.x;
              double dy = pt.y - last.y;
              double dist_sq = dx * dx + dy * dy;
              if (std::chrono::duration_cast<std::chrono::milliseconds>(
                      now - last_shot_time_[shooter_id - 1])
                          .count() < 200 &&
                  dist_sq < 0.05) {
                is_duplicate = true;
              }
            }
          }

          if (!is_duplicate &&
              std::chrono::duration_cast<std::chrono::milliseconds>(
                  now - last_shot_time_[shooter_id - 1])
                      .count() >= 300) {
            last_shot_time_[shooter_id - 1] = now;

            // 功能 A1：計算命中詳細資訊
            HitResult hit_result =
                hit_detection_svc_->evaluateHit(pt.x, pt.y, shooter_id);

            // 永遠記錄射擊點（測試模式 + 訓練模式都顯示紅點）
            {
              std::lock_guard<std::mutex> lock(shots_mutex_);
              long long ms =
                  std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::system_clock::now().time_since_epoch())
                      .count();

              ShotRecord shot;
              shot.shooter_id = shooter_id;
              shot.x = pt.x;
              shot.y = pt.y;
              shot.timestamp = ms;
              shot.sequence = ++shot_seq_;
              shot.camera = 'B';
              shot.intensity = p.area;
              shot.confidence = hit_result.confidence;
              shot.is_boundary_hit = hit_result.is_boundary_hit;
              shot.distance_from_center = hit_result.distance_from_center;

              latest_shots_.push_back(shot);
              if (latest_shots_.size() > 50)
                latest_shots_.erase(latest_shots_.begin());
            }
            // 只有訓練模式才計分和播放音效（前端根據 scoring_enabled 狀態決定）
          }
        }
      }
    }

    if (is_calibrating_) {
      bool a_done = false, b_done = false;
      {
        std::lock_guard<std::mutex> lock(calib_mutex_);
        a_done = (calib_points_a_.size() >= 4);
        b_done = (calib_points_b_.size() >= 4);
      }
      if (a_done || b_done)
        is_calibrating_ = false;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
}

void IRTracker::log(const std::string &msg) {
  auto now =
      std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
  struct tm buf;
  localtime_s(&buf, &now);

  std::ofstream f("t91_debug.log", std::ios::app);
  f << "[" << std::put_time(&buf, "%Y-%m-%d %H:%M:%S") << "] " << msg
    << std::endl;
  std::cout << msg << std::endl;
}

// ============================================================================
// 功能 B4：自動校準介面實作
// ============================================================================

void IRTracker::startAutoCalibrationA() {
  calibration_svc_->startAutoCalibration(CameraId::A);
  std::cout << "[IRTracker] Started auto calibration for Camera A" << std::endl;
}

void IRTracker::startAutoCalibrationB() {
  calibration_svc_->startAutoCalibration(CameraId::B);
  std::cout << "[IRTracker] Started auto calibration for Camera B" << std::endl;
}

json IRTracker::getCalibrationStatus() {
  CalibrationStatus status_a =
      calibration_svc_->getCalibrationStatus(CameraId::A);
  CalibrationStatus status_b =
      calibration_svc_->getCalibrationStatus(CameraId::B);

  auto modeToString = [](CalibrationMode mode) -> std::string {
    if (mode == CalibrationMode::NONE)
      return "none";
    if (mode == CalibrationMode::MANUAL_4POINT)
      return "manual_4point";
    if (mode == CalibrationMode::AUTO_CHESSBOARD)
      return "auto_chessboard";
    return "unknown";
  };

  json result = {{"camera_a",
                  {{"calibrated", status_a.calibrated},
                   {"mode", modeToString(status_a.mode)},
                   {"points_collected", status_a.points_collected},
                   {"corners_detected", status_a.corners_detected},
                   {"homography_determinant", status_a.homography_determinant},
                   {"last_error", status_a.last_error}}},
                 {"camera_b",
                  {{"calibrated", status_b.calibrated},
                   {"mode", modeToString(status_b.mode)},
                   {"points_collected", status_b.points_collected},
                   {"corners_detected", status_b.corners_detected},
                   {"homography_determinant", status_b.homography_determinant},
                   {"last_error", status_b.last_error}}}};

  return result;
}

} // namespace T91
