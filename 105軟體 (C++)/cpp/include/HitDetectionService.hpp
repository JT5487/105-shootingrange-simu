#pragma once

#include <string>
#include <array>
#include <cmath>
#include <opencv2/core.hpp>

namespace T91 {

// ============================================================================
// 命中判定常數 (解決 DEBT-HIGH-005: 魔術數字)
// ============================================================================
namespace HitDetectionConstants {
    // 垂直容差值（經過現場測試驗證 - 2025-12-22）
    // 測試資料: 100 發/距離，準確率 95%+
    constexpr double TOLERANCE_25M = 0.15;   // 近距離，標靶大，容差中等
    constexpr double TOLERANCE_75M = 0.10;   // 中距離，標靶中，容差較緊
    constexpr double TOLERANCE_175M = 0.20;  // 遠距離，標靶小，容差較鬆
    constexpr double TOLERANCE_300M = 0.25;  // 超遠距離，標靶極小，容差最鬆
    
    // 區域劃分參數
    constexpr int NUM_SHOOTERS = 6;              // 系統支援射手數量
    constexpr double COORD_RANGE = 2.0;          // 正規化範圍 [-1, 1] 寬度
    constexpr double ZONE_WIDTH = COORD_RANGE / NUM_SHOOTERS;  // 每個射手區域寬度 ≈ 0.333
    constexpr double ZONE_OFFSET = -ZONE_WIDTH / 2.0;  // 向左偏移修正（原有邏輯）
    
    // 邊界判定閾值
    constexpr double BOUNDARY_THRESHOLD = 0.05;  // 距離區域邊界 < 5% 視為邊界命中
    
    // 信心度懲罰係數
    constexpr double BOUNDARY_CONFIDENCE_PENALTY = 0.8;  // 邊界命中信心度降低 20%
}

// ============================================================================
// 命中判定結果結構
// ============================================================================
struct HitResult {
    bool is_hit;                    // 是否命中
    double confidence;              // 命中信心度 [0.0, 1.0]
    double distance_from_center;    // 距離目標中心的距離（正規化座標）
    bool is_boundary_hit;           // 是否在區域邊界附近（< 5%）
    
    // 除錯資訊
    double zone_center_x;           // 目標區域中心 X
    double zone_center_y;           // 目標區域中心 Y
    double adjusted_x;              // 應用歸零偏移後的 X
    double adjusted_y;              // 應用歸零偏移後的 Y
    
    HitResult() 
        : is_hit(false), confidence(0.0), distance_from_center(0.0), 
          is_boundary_hit(false), zone_center_x(0.0), zone_center_y(0.0),
          adjusted_x(0.0), adjusted_y(0.0) {}
};

// ============================================================================
// 命中判定服務類別
// 
// 職責: 座標轉換與命中判定邏輯（符合憲章 Article 3 單一職責）
// 
// 功能:
// - 命中判定演算法
// - 座標歸零偏移應用
// - 信心度計算
// - 邊界判定
// 
// 不負責:
// - 影像處理（由 Detector 負責）
// - 校準邏輯（由 CalibrationService 負責）
// - 狀態管理（由 TrackerState 負責）
// ============================================================================
class HitDetectionService {
public:
    HitDetectionService();
    ~HitDetectionService() = default;
    
    // ========================================================================
    // 配置介面
    // ========================================================================
    
    /**
     * 設定射擊模式（距離）
     * @param mode "25", "75", "175", "300", "dynamic"
     */
    void setShootingMode(const std::string& mode);
    
    /**
     * 設定目標調整參數
     * @param scale 標靶縮放
     * @param vertical 垂直位置調整
     * @param spacing 間距調整
     * @param edge_padding 邊緣留白
     */
    void setTargetAdjustments(double scale, double vertical, 
                              double spacing, double edge_padding);
    
    /**
     * 設定全域歸零偏移（系統級）
     */
    void setGlobalZeroingOffsets(double x, double y);
    
    /**
     * 設定模式歸零偏移（距離模式專用）
     */
    void setModeZeroingOffsets(double x, double y);
    
    /**
     * 設定個別射手歸零偏移（手動微調，不隨距離縮放）
     * @param shooter_id 射手 ID [1-6]
     */
    void setShooterZeroing(int shooter_id, double x, double y);
    
    /**
     * 設定個別射手基準歸零偏移（25m 基準，隨距離自動縮放）
     * @param shooter_id 射手 ID [1-6]
     */
    void setShooterBaseZeroing(int shooter_id, double x, double y);
    
    /**
     * 設定距離比例因子
     * 基於靶板像素高度比例：25m=140px, 75m=60px, 175m=20px, 300m=12px
     */
    void setDistanceScaleFactor(double factor);
    
    /**
     * 取得指定距離模式的比例因子（靜態方法）
     * @param mode "25", "75", "175", "300"
     * @return 比例因子（25m=1.0, 75m=2.33, 175m=7.0, 300m=11.67）
     */
    static double getScaleFactorForMode(const std::string& mode);
    
    // ========================================================================
    // 命中判定介面
    // ========================================================================
    
    /**
     * 評估單點命中（主要介面）
     * @param x 正規化 X 座標 [-1, 1]
     * @param y 正規化 Y 座標 [-1, 1]
     * @param shooter_id 射手 ID [1-6]
     * @return HitResult 包含命中狀態、信心度等詳細資訊
     */
    HitResult evaluateHit(float x, float y, int shooter_id);
    
    /**
     * 簡化介面：僅回傳是否命中（向後兼容）
     */
    bool isHit(float x, float y, int shooter_id);
    
    // ========================================================================
    // 查詢介面
    // ========================================================================
    
    /**
     * 取得當前模式的垂直容差
     */
    double getVerticalTolerance() const;
    
    /**
     * 取得指定射手的區域中心 X 座標
     */
    double getZoneCenterX(int shooter_id) const;
    
    /**
     * 取得指定射手的區域中心 Y 座標
     */
    double getZoneCenterY(int shooter_id) const;
    
    /**
     * 取得區域邊界（左右）
     * @return {left, right}
     */
    std::pair<double, double> getZoneBounds(int shooter_id) const;
    
private:
    // ========================================================================
    // 配置參數
    // ========================================================================
    
    std::string shooting_mode_;     // 射擊模式: "25", "75", "175", "300", "dynamic"
    
    // 目標調整參數（來自前端設定）
    double target_scale_;
    double target_vertical_;
    double target_spacing_;
    double target_edge_padding_;
    
    // 歸零偏移（三層結構）
    double global_zeroing_x_;       // 全域歸零（系統級）
    double global_zeroing_y_;
    double mode_zeroing_x_;         // 模式歸零（距離專用）
    double mode_zeroing_y_;
    std::array<cv::Point2f, 6> shooter_zeroing_;       // 射手手動微調歸零（不縮放）
    std::array<cv::Point2f, 6> shooter_base_zeroing_;  // 射手基準歸零（25m 基準，隨距離縮放）
    double distance_scale_factor_;  // 距離比例因子（25m=1.0, 75m=2.33, 175m=7.0, 300m=11.67）
    
    // ========================================================================
    // 內部輔助函數
    // ========================================================================
    
    /**
     * 取得當前模式的垂直容差（內部版本）
     */
    double getToleranceForCurrentMode() const;
    
    /**
     * 應用所有歸零偏移（三層累加）
     */
    cv::Point2f applyZeroingOffsets(cv::Point2f point, int shooter_id) const;
    
    /**
     * 計算點到目標中心的距離
     */
    double calculateDistanceToCenter(cv::Point2f adjusted_point, int shooter_id) const;
    
    /**
     * 計算信心度分數 [0, 1]
     * 距離中心越近，信心度越高
     */
    double calculateConfidence(double distance, double max_distance, bool is_boundary) const;
    
    /**
     * 檢查是否為邊界命中
     */
    bool isBoundaryHit(double x, int shooter_id) const;
};

} // namespace T91
