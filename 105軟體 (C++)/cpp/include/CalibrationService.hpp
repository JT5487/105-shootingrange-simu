#pragma once

#include <string>
#include <vector>
#include <optional>
#include <mutex>
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/calib3d.hpp>
#include <json.hpp>

using json = nlohmann::json;

namespace T91 {

// ============================================================================
// 相機識別碼
// ============================================================================
enum class CameraId {
    A,  // 左側相機（射手 1, 2, 3）
    B   // 右側相機（射手 4, 5, 6）
};

// ============================================================================
// 校準模式
// ============================================================================
enum class CalibrationMode {
    NONE,              // 無校準
    MANUAL_4POINT,     // 手動 4 點校準（當前方式）
    AUTO_CHESSBOARD    // 自動棋盤格校準（新增）
};

// ============================================================================
// 棋盤格配置
// ============================================================================
struct ChessboardConfig {
    int cols = 10;           // 內部角點列數（推薦 10，A3 紙張）
    int rows = 7;            // 內部角點行數（推薦 7，A3 紙張）
    float square_size = 30.0f; // 方格大小（單位：mm，實際物理尺寸）
    
    // 映射範圍（目標座標，與 IRTracker::dst_points_ ±0.9 一致）
    // 修正為 ±0.9 以匹配校準點的實際位置（5%-95%）
    float map_left = -0.9f;
    float map_right = 0.9f;
    float map_top = 0.9f;
    float map_bottom = -0.9f;
    
    ChessboardConfig() = default;
};

// ============================================================================
// 校準狀態
// ============================================================================
struct CalibrationStatus {
    bool calibrated = false;
    CalibrationMode mode = CalibrationMode::NONE;
    int points_collected = 0;  // 手動模式：已收集點數
    int corners_detected = 0;  // 自動模式：偵測到的角點數
    double homography_determinant = 0.0;  // Homography 行列式（判斷有效性）
    std::string last_error;  // 最後錯誤訊息
};

// ============================================================================
// 校準服務類別
//
// 職責: 相機校準邏輯（符合憲章 Article 3 單一職責）
//
// 功能:
// - 手動 4 點校準（原有功能）
// - 自動棋盤格校準（新增功能 B4）
// - Homography 計算與驗證
// - 校準資料持久化
//
// 不負責:
// - 影像抓取（由 CameraManager 負責）
// - IR 點偵測（由 Detector 負責）
// - 座標轉換應用（由 HitDetectionService 負責）
// ============================================================================
class CalibrationService {
public:
    CalibrationService();
    ~CalibrationService() = default;
    
    // ========================================================================
    // 手動校準介面（4 點 Homography）
    // ========================================================================
    
    /**
     * 開始手動校準
     * @param camera 相機 ID（A 或 B）
     */
    void startManualCalibration(CameraId camera);
    
    /**
     * 加入校準點
     * @param camera 相機 ID
     * @param point 相機座標中的點
     * @return true 如果成功加入，false 如果已滿 4 點
     */
    bool addCalibrationPoint(CameraId camera, cv::Point2f point);
    
    /**
     * 計算手動校準的 Homography
     * @param camera 相機 ID
     * @return true 如果成功計算
     */
    bool computeManualHomography(CameraId camera);
    
    /**
     * 取消校準
     */
    void cancelCalibration(CameraId camera);
    
    // ========================================================================
    // 自動校準介面（棋盤格）- 新增功能 B4
    // ========================================================================
    
    /**
     * 開始自動棋盤格校準
     * @param camera 相機 ID
     */
    void startAutoCalibration(CameraId camera);
    
    /**
     * 偵測棋盤格並計算 Homography
     * @param camera 相機 ID
     * @param frame 影像幀
     * @return true 如果成功偵測並計算
     */
    bool detectAndCalibrateChessboard(CameraId camera, const cv::Mat& frame);
    
    /**
     * 設定棋盤格配置
     */
    void setChessboardConfig(const ChessboardConfig& config);
    
    /**
     * 取得棋盤格配置
     */
    ChessboardConfig getChessboardConfig() const;
    
    // ========================================================================
    // 校準狀態查詢
    // ========================================================================
    
    /**
     * 檢查相機是否已校準
     */
    bool isCalibrated(CameraId camera) const;
    
    /**
     * 取得 Homography 矩陣
     * @return 3×3 轉換矩陣，若未校準則為空矩陣
     */
    cv::Mat getHomography(CameraId camera) const;
    
    /**
     * 取得當前校準模式
     */
    CalibrationMode getCurrentMode(CameraId camera) const;
    
    /**
     * 取得校準狀態詳細資訊
     */
    CalibrationStatus getCalibrationStatus(CameraId camera) const;
    
    /**
     * 取得當前正在校準的相機（若有）
     */
    std::optional<CameraId> getActiveCamera() const;
    
    // ========================================================================
    // 持久化介面
    // ========================================================================
    
    /**
     * 載入校準資料
     * @param filepath_a Camera A 的校準檔案路徑
     * @param filepath_b Camera B 的校準檔案路徑
     * @return true 如果至少一個相機載入成功
     */
    bool loadCalibration(const std::string& filepath_a, 
                         const std::string& filepath_b);
    
    /**
     * 儲存校準資料
     * @param filepath_a Camera A 的校準檔案路徑
     * @param filepath_b Camera B 的校準檔案路徑
     * @return true 如果至少一個相機儲存成功
     */
    bool saveCalibration(const std::string& filepath_a, 
                         const std::string& filepath_b);
    
private:
    // ========================================================================
    // 內部資料結構
    // ========================================================================
    
    struct CameraCalibration {
        // 手動校準資料
        std::vector<cv::Point2f> manual_points;
        
        // 自動校準資料
        std::vector<cv::Point2f> detected_corners;
        
        // 共用資料
        cv::Mat homography;
        bool valid = false;
        CalibrationMode mode = CalibrationMode::NONE;
        std::string last_error;
        
        CameraCalibration() = default;
    };
    
    CameraCalibration calib_a_;
    CameraCalibration calib_b_;
    ChessboardConfig chessboard_config_;
    std::optional<CameraId> active_camera_;
    
    mutable std::mutex calib_mutex_;  // 保護所有校準資料
    
    // ========================================================================
    // 內部輔助函數
    // ========================================================================
    
    /**
     * 取得相機校準資料（內部版本，需持有 mutex）
     */
    CameraCalibration& getCalibData(CameraId camera);
    const CameraCalibration& getCalibData(CameraId camera) const;
    
    /**
     * 偵測棋盤格角點（內部實作）
     */
    bool detectChessboardInternal(CameraCalibration& calib, const cv::Mat& frame);
    
    /**
     * 生成世界座標點（棋盤格角點的目標位置）
     */
    std::vector<cv::Point2f> generateWorldPoints() const;
    
    /**
     * 驗證 Homography 矩陣有效性
     * @return true 如果矩陣有效（非奇異）
     */
    bool validateHomography(const cv::Mat& H) const;
    
    /**
     * 載入單一相機校準資料
     */
    bool loadSingleCalibration(CameraId camera, const std::string& filepath);
    
    /**
     * 儲存單一相機校準資料
     */
    bool saveSingleCalibration(CameraId camera, const std::string& filepath);
    
    /**
     * 轉換 CameraId 為字串
     */
    static std::string cameraIdToString(CameraId camera);
};

} // namespace T91
