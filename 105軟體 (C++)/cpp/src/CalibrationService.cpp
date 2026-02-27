#include "CalibrationService.hpp"
#include <iostream>
#include <fstream>
#include <cmath>

namespace T91 {

// ============================================================================
// 建構子
// ============================================================================

CalibrationService::CalibrationService() 
    : active_camera_(std::nullopt)
{
    std::cout << "[Calibration] Service initialized" << std::endl;
}

// ============================================================================
// 手動校準介面實作
// ============================================================================

void CalibrationService::startManualCalibration(CameraId camera) {
    std::lock_guard<std::mutex> lock(calib_mutex_);
    
    auto& calib = getCalibData(camera);
    calib.manual_points.clear();
    calib.valid = false;
    calib.mode = CalibrationMode::MANUAL_4POINT;
    calib.last_error.clear();
    
    active_camera_ = camera;
    
    std::cout << "[Calibration] Started manual calibration for Camera " 
              << cameraIdToString(camera) << std::endl;
}

bool CalibrationService::addCalibrationPoint(CameraId camera, cv::Point2f point) {
    std::lock_guard<std::mutex> lock(calib_mutex_);
    
    auto& calib = getCalibData(camera);
    
    if (calib.manual_points.size() >= 4) {
        std::cerr << "[Calibration] Already have 4 points" << std::endl;
        return false;
    }
    
    calib.manual_points.push_back(point);
    std::cout << "[Calibration] Point " << calib.manual_points.size() 
              << "/4 added: (" << point.x << ", " << point.y << ")" << std::endl;
    
    return true;
}

bool CalibrationService::computeManualHomography(CameraId camera) {
    std::lock_guard<std::mutex> lock(calib_mutex_);
    
    auto& calib = getCalibData(camera);
    
    if (calib.manual_points.size() != 4) {
        calib.last_error = "Need exactly 4 points, got " + std::to_string(calib.manual_points.size());
        std::cerr << "[Calibration] " << calib.last_error << std::endl;
        return false;
    }
    
    // 目標點：與 IRTracker::dst_points_ 及 ChessboardConfig 統一使用 ±0.9
    // 校準點位於投影幕的 5%~95% 位置，避免邊緣畸變
    std::vector<cv::Point2f> dst_points = {
        cv::Point2f(-0.9f,  0.9f),  // 左上 (5%, 5%)
        cv::Point2f( 0.9f,  0.9f),  // 右上 (95%, 5%)
        cv::Point2f( 0.9f, -0.9f),  // 右下 (95%, 95%)
        cv::Point2f(-0.9f, -0.9f)   // 左下 (5%, 95%)
    };
    
    // 計算 Homography
    calib.homography = cv::findHomography(calib.manual_points, dst_points);
    
    // 驗證
    if (!validateHomography(calib.homography)) {
        calib.last_error = "Homography is singular or invalid";
        std::cerr << "[Calibration] " << calib.last_error << std::endl;
        return false;
    }
    
    calib.valid = true;
    calib.mode = CalibrationMode::MANUAL_4POINT;
    active_camera_ = std::nullopt;
    
    std::cout << "[Calibration] Manual calibration computed for Camera " 
              << cameraIdToString(camera) << std::endl;
    
    return true;
}

void CalibrationService::cancelCalibration(CameraId camera) {
    std::lock_guard<std::mutex> lock(calib_mutex_);
    
    auto& calib = getCalibData(camera);
    calib.manual_points.clear();
    calib.detected_corners.clear();
    active_camera_ = std::nullopt;
    
    std::cout << "[Calibration] Calibration cancelled for Camera " 
              << cameraIdToString(camera) << std::endl;
}

// ============================================================================
// 自動校準介面實作（新增功能 B4）
// ============================================================================

void CalibrationService::startAutoCalibration(CameraId camera) {
    std::lock_guard<std::mutex> lock(calib_mutex_);
    
    auto& calib = getCalibData(camera);
    calib.detected_corners.clear();
    calib.valid = false;
    calib.mode = CalibrationMode::AUTO_CHESSBOARD;
    calib.last_error.clear();
    
    active_camera_ = camera;
    
    std::cout << "[Calibration] Started auto calibration (chessboard) for Camera " 
              << cameraIdToString(camera) 
              << " (" << chessboard_config_.cols << "x" << chessboard_config_.rows << ")" 
              << std::endl;
}

bool CalibrationService::detectAndCalibrateChessboard(CameraId camera, const cv::Mat& frame) {
    std::lock_guard<std::mutex> lock(calib_mutex_);
    
    auto& calib = getCalibData(camera);
    
    // 1. 偵測棋盤格
    if (!detectChessboardInternal(calib, frame)) {
        return false;
    }
    
    // 2. 生成世界座標
    std::vector<cv::Point2f> world_points = generateWorldPoints();
    
    if (world_points.size() != calib.detected_corners.size()) {
        calib.last_error = "Point count mismatch";
        std::cerr << "[Calibration] " << calib.last_error << std::endl;
        return false;
    }
    
    // 3. 計算 Homography (使用 RANSAC 提高魯棒性)
    calib.homography = cv::findHomography(
        calib.detected_corners,
        world_points,
        cv::RANSAC,
        3.0  // RANSAC 閾值
    );
    
    // 4. 驗證
    if (!validateHomography(calib.homography)) {
        calib.last_error = "Homography is singular or invalid";
        std::cerr << "[Calibration] " << calib.last_error << std::endl;
        return false;
    }
    
    // 5. 校準成功
    calib.valid = true;
    calib.mode = CalibrationMode::AUTO_CHESSBOARD;
    active_camera_ = std::nullopt;
    
    std::cout << "[Calibration] Auto calibration successful for Camera " 
              << cameraIdToString(camera) 
              << " (detected " << calib.detected_corners.size() << " corners)" 
              << std::endl;
    
    return true;
}

void CalibrationService::setChessboardConfig(const ChessboardConfig& config) {
    std::lock_guard<std::mutex> lock(calib_mutex_);
    chessboard_config_ = config;
    
    std::cout << "[Calibration] Chessboard config set: " 
              << config.cols << "x" << config.rows << std::endl;
}

ChessboardConfig CalibrationService::getChessboardConfig() const {
    std::lock_guard<std::mutex> lock(calib_mutex_);
    return chessboard_config_;
}

// ============================================================================
// 校準狀態查詢實作
// ============================================================================

bool CalibrationService::isCalibrated(CameraId camera) const {
    std::lock_guard<std::mutex> lock(calib_mutex_);
    return getCalibData(camera).valid;
}

cv::Mat CalibrationService::getHomography(CameraId camera) const {
    std::lock_guard<std::mutex> lock(calib_mutex_);
    return getCalibData(camera).homography.clone();
}

CalibrationMode CalibrationService::getCurrentMode(CameraId camera) const {
    std::lock_guard<std::mutex> lock(calib_mutex_);
    return getCalibData(camera).mode;
}

CalibrationStatus CalibrationService::getCalibrationStatus(CameraId camera) const {
    std::lock_guard<std::mutex> lock(calib_mutex_);
    
    const auto& calib = getCalibData(camera);
    
    CalibrationStatus status;
    status.calibrated = calib.valid;
    status.mode = calib.mode;
    status.points_collected = static_cast<int>(calib.manual_points.size());
    status.corners_detected = static_cast<int>(calib.detected_corners.size());
    status.last_error = calib.last_error;
    
    if (!calib.homography.empty()) {
        status.homography_determinant = cv::determinant(calib.homography);
    }
    
    return status;
}

std::optional<CameraId> CalibrationService::getActiveCamera() const {
    std::lock_guard<std::mutex> lock(calib_mutex_);
    return active_camera_;
}

// ============================================================================
// 持久化介面實作
// ============================================================================

bool CalibrationService::loadCalibration(const std::string& filepath_a, 
                                          const std::string& filepath_b) {
    bool success_a = loadSingleCalibration(CameraId::A, filepath_a);
    bool success_b = loadSingleCalibration(CameraId::B, filepath_b);
    
    return success_a || success_b;
}

bool CalibrationService::saveCalibration(const std::string& filepath_a, 
                                          const std::string& filepath_b) {
    bool success_a = saveSingleCalibration(CameraId::A, filepath_a);
    bool success_b = saveSingleCalibration(CameraId::B, filepath_b);
    
    return success_a || success_b;
}

// ============================================================================
// 內部輔助函數實作
// ============================================================================

CalibrationService::CameraCalibration& CalibrationService::getCalibData(CameraId camera) {
    return (camera == CameraId::A) ? calib_a_ : calib_b_;
}

const CalibrationService::CameraCalibration& CalibrationService::getCalibData(CameraId camera) const {
    return (camera == CameraId::A) ? calib_a_ : calib_b_;
}

bool CalibrationService::detectChessboardInternal(CameraCalibration& calib, const cv::Mat& frame) {
    // 轉換為灰階（若需要）
    cv::Mat gray;
    if (frame.channels() == 3) {
        cv::cvtColor(frame, gray, cv::COLOR_BGR2GRAY);
    } else {
        gray = frame;
    }
    
    // 偵測棋盤格角點
    cv::Size patternSize(chessboard_config_.cols, chessboard_config_.rows);
    std::vector<cv::Point2f> corners;
    
    bool found = cv::findChessboardCorners(
        gray,
        patternSize,
        corners,
        cv::CALIB_CB_ADAPTIVE_THRESH | 
        cv::CALIB_CB_NORMALIZE_IMAGE |
        cv::CALIB_CB_FAST_CHECK
    );
    
    if (!found) {
        calib.last_error = "Chessboard not detected";
        return false;
    }
    
    // 亞像素精度優化
    cv::TermCriteria criteria(
        cv::TermCriteria::EPS + cv::TermCriteria::MAX_ITER,
        30,   // 最大迭代次數
        0.001 // 精度
    );
    
    cv::cornerSubPix(gray, corners, cv::Size(11, 11), cv::Size(-1, -1), criteria);
    
    // 儲存偵測結果
    calib.detected_corners = corners;
    
    std::cout << "[Calibration] Chessboard detected: " << corners.size() 
              << " corners" << std::endl;
    
    return true;
}

std::vector<cv::Point2f> CalibrationService::generateWorldPoints() const {
    std::vector<cv::Point2f> world_points;
    
    // 棋盤格角點映射到正規化座標 [-1.0, 1.0]（統一座標空間）
    float x_step = (chessboard_config_.map_right - chessboard_config_.map_left) 
                   / (chessboard_config_.cols - 1);
    float y_step = (chessboard_config_.map_top - chessboard_config_.map_bottom) 
                   / (chessboard_config_.rows - 1);
    
    for (int row = 0; row < chessboard_config_.rows; row++) {
        for (int col = 0; col < chessboard_config_.cols; col++) {
            float x = chessboard_config_.map_left + col * x_step;
            float y = chessboard_config_.map_top - row * y_step;
            world_points.push_back(cv::Point2f(x, y));
        }
    }
    
    return world_points;
}

bool CalibrationService::validateHomography(const cv::Mat& H) const {
    if (H.empty()) {
        return false;
    }
    
    if (H.rows != 3 || H.cols != 3) {
        return false;
    }
    
    // 檢查行列式（非奇異）
    double det = cv::determinant(H);
    if (std::abs(det) < 1e-6) {
        std::cerr << "[Calibration] Homography determinant too small: " << det << std::endl;
        return false;
    }
    
    std::cout << "[Calibration] Homography determinant: " << det << std::endl;
    return true;
}

bool CalibrationService::loadSingleCalibration(CameraId camera, const std::string& filepath) {
    std::lock_guard<std::mutex> lock(calib_mutex_);
    
    std::ifstream ifs(filepath);
    if (!ifs.is_open()) {
        std::cerr << "[Calibration] Cannot open file: " << filepath << std::endl;
        return false;
    }
    
    try {
        json j;
        ifs >> j;
        
        auto& calib = getCalibData(camera);
        
        // [BugFix] 支援兩種檔案格式：
        //   新格式: {"calibration": {"valid": true, "homography": [[...]]}}
        //   舊格式: {"homography": [[...]]}  (現有檔案使用此格式)
        json homography_data;
        bool found_homography = false;
        
        if (j.contains("calibration") && j["calibration"].contains("valid")) {
            // 新格式
            bool valid = j["calibration"]["valid"];
            if (!valid) {
                std::cout << "[Calibration] Calibration not valid in file" << std::endl;
                return false;
            }
            if (j["calibration"].contains("homography") && 
                j["calibration"]["homography"].is_array() &&
                j["calibration"]["homography"].size() == 3) {
                homography_data = j["calibration"]["homography"];
                found_homography = true;
            }
        } else if (j.contains("homography") && j["homography"].is_array() && 
                   j["homography"].size() == 3) {
            // 舊格式（直接包含 homography 陣列）
            homography_data = j["homography"];
            found_homography = true;
        }
        
        if (!found_homography) {
            std::cerr << "[Calibration] No valid homography found in file: " << filepath << std::endl;
            return false;
        }
        
        cv::Mat H(3, 3, CV_64F);
        for (int i = 0; i < 3; i++) {
            for (int j_idx = 0; j_idx < 3; j_idx++) {
                H.at<double>(i, j_idx) = homography_data[i][j_idx];
            }
        }
        
        calib.homography = H;
        calib.valid = true;
        calib.mode = CalibrationMode::MANUAL_4POINT;  // 預設為手動模式
        
        std::cout << "[Calibration] Loaded calibration for Camera " 
                  << cameraIdToString(camera) << std::endl;
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "[Calibration] Error loading calibration: " << e.what() << std::endl;
        return false;
    }
    
    return false;
}

bool CalibrationService::saveSingleCalibration(CameraId camera, const std::string& filepath) {
    std::lock_guard<std::mutex> lock(calib_mutex_);
    
    const auto& calib = getCalibData(camera);
    
    if (!calib.valid) {
        std::cerr << "[Calibration] Cannot save invalid calibration" << std::endl;
        return false;
    }
    
    try {
        json j;
        j["calibration"]["valid"] = true;
        
        // 儲存 homography
        json h_array = json::array();
        for (int i = 0; i < 3; i++) {
            json row = json::array();
            for (int j_idx = 0; j_idx < 3; j_idx++) {
                row.push_back(calib.homography.at<double>(i, j_idx));
            }
            h_array.push_back(row);
        }
        j["calibration"]["homography"] = h_array;
        
        // 儲存模式
        if (calib.mode == CalibrationMode::MANUAL_4POINT) {
            j["calibration"]["mode"] = "manual_4point";
        } else if (calib.mode == CalibrationMode::AUTO_CHESSBOARD) {
            j["calibration"]["mode"] = "auto_chessboard";
        }
        
        // 寫入檔案
        std::ofstream ofs(filepath);
        if (!ofs.is_open()) {
            std::cerr << "[Calibration] Cannot open file for writing: " << filepath << std::endl;
            return false;
        }
        
        ofs << j.dump(2);  // Pretty print with 2-space indent
        
        std::cout << "[Calibration] Saved calibration for Camera " 
                  << cameraIdToString(camera) << " to " << filepath << std::endl;
        
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "[Calibration] Error saving calibration: " << e.what() << std::endl;
        return false;
    }
}

std::string CalibrationService::cameraIdToString(CameraId camera) {
    return (camera == CameraId::A) ? "A" : "B";
}

} // namespace T91
