#include "HitDetectionService.hpp"
#include <algorithm>
#include <iostream>


namespace T91 {

using namespace HitDetectionConstants;

// ============================================================================
// 建構子
// ============================================================================

HitDetectionService::HitDetectionService()
    : shooting_mode_("25"), target_scale_(1.0), target_vertical_(0.0),
      target_spacing_(0.0), target_edge_padding_(0.0), global_zeroing_x_(0.0),
      global_zeroing_y_(0.0), mode_zeroing_x_(0.0), mode_zeroing_y_(0.0),
      distance_scale_factor_(1.0) {
  // 初始化射手歸零偏移為零
  for (int i = 0; i < 6; ++i) {
    shooter_zeroing_[i] = cv::Point2f(0.0f, 0.0f);
    shooter_base_zeroing_[i] = cv::Point2f(0.0f, 0.0f);
  }

  std::cout << "[HitDetection] Service initialized" << std::endl;
}

// ============================================================================
// 配置介面實作
// ============================================================================

void HitDetectionService::setShootingMode(const std::string &mode) {
  shooting_mode_ = mode;
  // 自動更新距離比例因子
  distance_scale_factor_ = getScaleFactorForMode(mode);
  std::cout << "[HitDetection] Mode set to: " << mode
            << " (tolerance: " << getToleranceForCurrentMode()
            << ", scaleFactor: " << distance_scale_factor_ << ")" << std::endl;
}

void HitDetectionService::setTargetAdjustments(double scale, double vertical,
                                               double spacing,
                                               double edge_padding) {
  target_scale_ = scale;
  target_vertical_ = vertical;
  target_spacing_ = spacing;
  target_edge_padding_ = edge_padding;

  std::cout << "[HitDetection] Target adjustments - Scale:" << scale
            << " Vertical:" << vertical << " Spacing:" << spacing
            << " Edge:" << edge_padding << std::endl;
}

void HitDetectionService::setGlobalZeroingOffsets(double x, double y) {
  global_zeroing_x_ = x;
  global_zeroing_y_ = y;
  std::cout << "[HitDetection] Global zeroing - X:" << x << " Y:" << y
            << std::endl;
}

void HitDetectionService::setModeZeroingOffsets(double x, double y) {
  mode_zeroing_x_ = x;
  mode_zeroing_y_ = y;
  std::cout << "[HitDetection] Mode zeroing - X:" << x << " Y:" << y
            << std::endl;
}

void HitDetectionService::setShooterZeroing(int shooter_id, double x,
                                            double y) {
  if (shooter_id < 1 || shooter_id > 6) {
    std::cerr << "[HitDetection] Invalid shooter_id: " << shooter_id
              << std::endl;
    return;
  }

  shooter_zeroing_[shooter_id - 1] = cv::Point2f(x, y);
  std::cout << "[HitDetection] Shooter " << shooter_id
            << " manual zeroing - X:" << x << " Y:" << y << std::endl;
}

void HitDetectionService::setShooterBaseZeroing(int shooter_id, double x,
                                                double y) {
  if (shooter_id < 1 || shooter_id > 6) {
    std::cerr << "[HitDetection] Invalid shooter_id: " << shooter_id
              << std::endl;
    return;
  }

  shooter_base_zeroing_[shooter_id - 1] = cv::Point2f(x, y);
  std::cout << "[HitDetection] Shooter " << shooter_id
            << " base zeroing (25m) - X:" << x << " Y:" << y
            << " (effective at current scale " << distance_scale_factor_
            << "x: X=" << x * distance_scale_factor_
            << " Y=" << y * distance_scale_factor_ << ")" << std::endl;
}

void HitDetectionService::setDistanceScaleFactor(double factor) {
  distance_scale_factor_ = factor;
  std::cout << "[HitDetection] Distance scale factor set to: " << factor
            << std::endl;
}

double HitDetectionService::getScaleFactorForMode(const std::string &mode) {
  // 基於靶板像素高度比例：25m=140px, 75m=60px, 175m=20px, 300m=12px
  // 比例因子 = 140 / 當前距離靶板高度
  if (mode == "25")
    return 1.0; // 140/140
  if (mode == "75")
    return 140.0 / 60.0; // ≈ 2.33
  if (mode == "175")
    return 140.0 / 20.0; // = 7.0
  if (mode == "300")
    return 140.0 / 12.0; // ≈ 11.67
  return 1.0;            // dynamic 或未知模式預設 1.0
}

// ============================================================================
// 命中判定介面實作
// ============================================================================

HitResult HitDetectionService::evaluateHit(float x, float y, int shooter_id) {
  HitResult result;

  // 驗證射手 ID
  if (shooter_id < 1 || shooter_id > NUM_SHOOTERS) {
    std::cerr << "[HitDetection] Invalid shooter_id: " << shooter_id
              << std::endl;
    return result; // is_hit = false
  }

  // 1. 座標已在 IRTracker 處理迴圈中完成三層歸零偏移
  //    (global + mode + shooter)，此處直接使用傳入值
  //    [BugFix] 移除 applyZeroingOffsets() 避免重複套用歸零
  cv::Point2f adjusted(x, y);
  result.adjusted_x = adjusted.x;
  result.adjusted_y = adjusted.y;

  // 2. 計算區域邊界
  double zone_left = -1.0 + (shooter_id - 1) * ZONE_WIDTH + ZONE_OFFSET;
  double zone_right = zone_left + ZONE_WIDTH;

  // 3. 檢查 X 座標是否在區域內
  if (adjusted.x < zone_left || adjusted.x >= zone_right) {
    return result; // is_hit = false
  }

  // 4. 計算目標中心
  result.zone_center_x = (zone_left + zone_right) / 2.0;
  result.zone_center_y = -target_vertical_ * 2.0; // 原有邏輯

  // 5. 取得垂直容差
  double tolerance = getToleranceForCurrentMode();

  // 6. 檢查 Y 座標是否在容差範圍內
  double dy = std::abs(adjusted.y - result.zone_center_y);
  if (dy > tolerance) {
    return result; // is_hit = false
  }

  // 7. 命中！計算詳細資訊
  result.is_hit = true;

  // 8. 計算距離中心的距離
  result.distance_from_center = calculateDistanceToCenter(adjusted, shooter_id);

  // 9. 檢查是否為邊界命中
  result.is_boundary_hit = isBoundaryHit(adjusted.x, shooter_id);

  // 10. 計算信心度
  result.confidence = calculateConfidence(result.distance_from_center,
                                          tolerance, result.is_boundary_hit);

  return result;
}

bool HitDetectionService::isHit(float x, float y, int shooter_id) {
  return evaluateHit(x, y, shooter_id).is_hit;
}

// ============================================================================
// 查詢介面實作
// ============================================================================

double HitDetectionService::getVerticalTolerance() const {
  return getToleranceForCurrentMode();
}

double HitDetectionService::getZoneCenterX(int shooter_id) const {
  if (shooter_id < 1 || shooter_id > NUM_SHOOTERS) {
    return 0.0;
  }

  double zone_left = -1.0 + (shooter_id - 1) * ZONE_WIDTH + ZONE_OFFSET;
  double zone_right = zone_left + ZONE_WIDTH;
  return (zone_left + zone_right) / 2.0;
}

double HitDetectionService::getZoneCenterY(int shooter_id) const {
  return -target_vertical_ * 2.0;
}

std::pair<double, double>
HitDetectionService::getZoneBounds(int shooter_id) const {
  if (shooter_id < 1 || shooter_id > NUM_SHOOTERS) {
    return {0.0, 0.0};
  }

  double zone_left = -1.0 + (shooter_id - 1) * ZONE_WIDTH + ZONE_OFFSET;
  double zone_right = zone_left + ZONE_WIDTH;
  return {zone_left, zone_right};
}

// ============================================================================
// 內部輔助函數實作
// ============================================================================

double HitDetectionService::getToleranceForCurrentMode() const {
  // 使用命名常數（解決 DEBT-HIGH-005）
  if (shooting_mode_ == "25") {
    return TOLERANCE_25M;
  } else if (shooting_mode_ == "75") {
    return TOLERANCE_75M;
  } else if (shooting_mode_ == "175") {
    return TOLERANCE_175M;
  } else if (shooting_mode_ == "300") {
    return TOLERANCE_300M;
  } else if (shooting_mode_ == "dynamic") {
    // 動態模式：無容差檢查（所有射擊有效）
    return 999.0; // 極大值
  } else {
    // 預設值
    return TOLERANCE_25M;
  }
}

cv::Point2f HitDetectionService::applyZeroingOffsets(cv::Point2f point,
                                                     int shooter_id) const {
  // 三層歸零偏移累加（優先順序：全域 → 模式 → 射手）
  cv::Point2f result = point;

  // Layer 1: 全域歸零
  result.x += global_zeroing_x_;
  result.y += global_zeroing_y_;

  // Layer 2: 模式歸零
  result.x += mode_zeroing_x_;
  result.y += mode_zeroing_y_;

  // Layer 3: 射手歸零 = (基準偏移 × 距離比例因子) + 手動微調
  if (shooter_id >= 1 && shooter_id <= 6) {
    // 基準偏移按距離比例縮放
    result.x +=
        shooter_base_zeroing_[shooter_id - 1].x * distance_scale_factor_;
    result.y +=
        shooter_base_zeroing_[shooter_id - 1].y * distance_scale_factor_;
    // 手動微調（固定值，不縮放）
    result.x += shooter_zeroing_[shooter_id - 1].x;
    result.y += shooter_zeroing_[shooter_id - 1].y;
  }

  return result;
}

double
HitDetectionService::calculateDistanceToCenter(cv::Point2f adjusted_point,
                                               int shooter_id) const {
  double center_x = getZoneCenterX(shooter_id);
  double center_y = getZoneCenterY(shooter_id);

  double dx = adjusted_point.x - center_x;
  double dy = adjusted_point.y - center_y;

  return std::sqrt(dx * dx + dy * dy);
}

double HitDetectionService::calculateConfidence(double distance,
                                                double max_distance,
                                                bool is_boundary) const {
  // 基礎信心度：距離中心越近越高
  // 線性映射：distance = 0 → confidence = 1.0
  //          distance = max_distance → confidence = 0.0
  double base_confidence = 1.0 - (distance / max_distance);

  // 確保在 [0, 1] 範圍內
  base_confidence = std::max(0.0, std::min(1.0, base_confidence));

  // 邊界懲罰
  if (is_boundary) {
    base_confidence *= BOUNDARY_CONFIDENCE_PENALTY;
  }

  return base_confidence;
}

bool HitDetectionService::isBoundaryHit(double x, int shooter_id) const {
  auto [zone_left, zone_right] = getZoneBounds(shooter_id);

  // 檢查是否接近左右邊界
  bool near_left = (x - zone_left) < BOUNDARY_THRESHOLD;
  bool near_right = (zone_right - x) < BOUNDARY_THRESHOLD;

  return near_left || near_right;
}

} // namespace T91
