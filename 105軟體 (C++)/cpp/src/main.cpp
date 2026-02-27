#include "IRTracker.hpp"
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <httplib.h>
#include <iostream>
#include <json.hpp>
#include <thread>

using namespace T91;
using json = nlohmann::json;

// 全域變數：儲存所有環境的靶板調整參數
// 格式: { "day_25": {...}, "night_175": {...}, ... } 共 8 種組合
// (天候不影響靶位)
json allTargetSettings = {};

// 當前環境設定
std::string currentWeather = "sunny"; // 只影響視覺效果，不影響靶位
std::string currentTime = "day";
std::string currentDistance = "25"; // 射擊距離 (25, 75, 175, dynamic)

// 取得當前環境鍵 (時段_距離，不含天候)
std::string getEnvironmentKey() { return currentTime + "_" + currentDistance; }

// 預設的靶板設定
json getDefaultTargetSettings() {
  return {{"scale", 1.0},       {"vertical", 0.0}, {"spacing", 1.0},
          {"edgePadding", 1.0}, {"zeroingX", 0.0}, {"zeroingY", 0.0}};
}

// 預設的射手設定（6名射手）
json getDefaultShooterSettings() {
  json shooters = json::array();
  for (int i = 1; i <= 6; ++i) {
    shooters.push_back({{"id", i}, {"zeroingX", 0.0}, {"zeroingY", 0.0}});
  }
  return shooters;
}

// 取得當前環境的靶板設定
json getCurrentTargetSettings() {
  std::string key = getEnvironmentKey();
  if (allTargetSettings.contains(key)) {
    return allTargetSettings[key];
  }
  return getDefaultTargetSettings();
}

// 載入當前環境的射手歸零到 tracker
void loadCurrentShooterZeroing(IRTracker& tracker) {
  std::string key = getEnvironmentKey();
  
  // 確保當前環境有 shooters 陣列
  if (!allTargetSettings.contains(key)) {
    allTargetSettings[key] = getDefaultTargetSettings();
  }
  if (!allTargetSettings[key].contains("shooters")) {
    allTargetSettings[key]["shooters"] = getDefaultShooterSettings();
  }
  
  // 載入射手歸零值
  for (auto &s : allTargetSettings[key]["shooters"]) {
    int id = s["id"];
    tracker.setShooterZeroing(id, s.value("zeroingX", 0.0), s.value("zeroingY", 0.0));
  }
  
  T91::IRTracker::log("Loaded shooter zeroing for: " + key);
}

// 靶板設定檔路徑
const std::string TARGET_SETTINGS_FILE = "target_settings.json";

// 載入所有環境的靶板設定
void loadAllTargetSettings() {
  try {
    std::ifstream f(TARGET_SETTINGS_FILE);
    if (f.is_open()) {
      allTargetSettings = json::parse(f);
      T91::IRTracker::log("Loaded all target settings from " +
                          TARGET_SETTINGS_FILE);
    } else {
      // 初始化預設設定
      allTargetSettings = {{"sunny_day", getDefaultTargetSettings()},
                           {"sunny_night", getDefaultTargetSettings()},
                           {"rainy_day", getDefaultTargetSettings()},
                           {"rainy_night", getDefaultTargetSettings()}};
      T91::IRTracker::log("No target settings file found, using defaults for "
                          "all environments.");
    }
  } catch (const std::exception &e) {
    T91::IRTracker::log("Error loading target settings: " +
                        std::string(e.what()));
    allTargetSettings = {{"sunny_day", getDefaultTargetSettings()},
                         {"sunny_night", getDefaultTargetSettings()},
                         {"rainy_day", getDefaultTargetSettings()},
                         {"rainy_night", getDefaultTargetSettings()}};
  }
}

// 儲存所有環境的靶板設定
void saveAllTargetSettings() {
  try {
    std::ofstream f(TARGET_SETTINGS_FILE);
    f << allTargetSettings.dump(4);
    T91::IRTracker::log("All target settings saved to " + TARGET_SETTINGS_FILE);
  } catch (const std::exception &e) {
    T91::IRTracker::log("Error saving target settings: " +
                        std::string(e.what()));
  }
}

int main() {
  T91::IRTracker::log("--- SYSTEM START ---");
  try {
    IRTracker tracker;
    T91::IRTracker::log("Initializing tracker...");
    if (!tracker.initialize()) {
      T91::IRTracker::log("FATAL: Initialization failed.");
      return 1;
    }

    T91::IRTracker::log("Starting processing thread...");
    tracker.start();

    // 載入所有環境的靶板設定並同步當前環境到 tracker
    loadAllTargetSettings();
    json currentSettings = getCurrentTargetSettings();
    tracker.setTargetAdjustments(currentSettings["scale"].get<double>(),
                                 currentSettings["vertical"].get<double>(),
                                 currentSettings["spacing"].get<double>(),
                                 currentSettings["edgePadding"].get<double>());
    tracker.setZeroingOffsets(currentSettings.value("zeroingX", 0.0),
                              currentSettings.value("zeroingY", 0.0));

    // 載入與同步全域歸零設定
    if (allTargetSettings.contains("global")) {
      tracker.setGlobalZeroingOffsets(
          allTargetSettings["global"].value("zeroingX", 0.0),
          allTargetSettings["global"].value("zeroingY", 0.0));
    }

    // 載入當前環境的射手歸零值（按距離分開存儲）
    loadCurrentShooterZeroing(tracker);

    // 載入全域射手強度特徵（不隨距離變化）
    if (allTargetSettings.contains("shooters_intensity")) {
      for (auto &s : allTargetSettings["shooters_intensity"]) {
        int id = s["id"];
        if (s.contains("intensity")) {
          tracker.setShooterIntensity(id, s["intensity"].get<int>());
        }
        if (s.contains("minIntensity")) {
          tracker.setShooterMinIntensity(id, s["minIntensity"].get<int>());
        }
      }
    }
    
    if (allTargetSettings.contains("useIntensityId")) {
      tracker.setIntensityIdEnabled(
          allTargetSettings["useIntensityId"].get<bool>());
    }

    httplib::Server svr;
    svr.set_post_routing_handler([](const auto &req, auto &res) {
      res.set_header("Access-Control-Allow-Origin", "*");
      res.set_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
      res.set_header("Access-Control-Allow-Headers", "Content-Type");
    });

    svr.Options(R"(/.*)", [](const auto &req, auto &res) { res.status = 200; });

    svr.Post("/command", [&](const httplib::Request &req,
                             httplib::Response &res) {
      try {
        auto j = json::parse(req.body);
        std::string action = j["action"];
        json data = j.value("data", json::object());
        if (action == "update_threshold")
          tracker.updateThreshold(data.value("threshold", 200));
        else if (action == "reset_hits")
          tracker.resetHits();
        else if (action == "start_calibration")
          tracker.startCalibration();
        else if (action == "save_results")
          tracker.saveResults(data);
        else if (action == "start_scoring")
          tracker.startScoring();
        else if (action == "stop_scoring")
          tracker.stopScoring();
        else if (action == "register_hit") {
          // 前端通知後端：有命中
          int shooterId = data.value("shooter_id", 1);
          if (shooterId >= 1 && shooterId <= 6) {
            tracker.registerHit(shooterId);
          }
        } else if (action == "change_mode") {
          std::string mode = data.value("mode", "25");
          currentDistance = mode;
          tracker.setShootingMode(mode);

          // 切換射擊模式時也要載入對應環境的靶板設定
          json settings = getCurrentTargetSettings();
          tracker.setTargetAdjustments(settings["scale"].get<double>(),
                                       settings["vertical"].get<double>(),
                                       settings["spacing"].get<double>(),
                                       settings["edgePadding"].get<double>());
          tracker.setZeroingOffsets(settings.value("zeroingX", 0.0),
                                    settings.value("zeroingY", 0.0));
          
          // 載入新距離模式的射手歸零值
          loadCurrentShooterZeroing(tracker);
          
          T91::IRTracker::log("Mode switched to: " + getEnvironmentKey());
        } else if (action == "apply_settings") {
          std::string distance = data.value("distance", "25");
          currentDistance = distance;
          tracker.setShootingMode(distance);

          // 更新當前環境並載入對應的靶板設定
          currentTime = data.value("time", "day");
          currentWeather = data.value("weather", "sunny");
          tracker.setTimeOfDay(currentTime);
          tracker.setWeather(currentWeather);

          // 載入新環境的靶板設定並同步到 tracker
          json settings = getCurrentTargetSettings();
          tracker.setTargetAdjustments(settings["scale"].get<double>(),
                                       settings["vertical"].get<double>(),
                                       settings["spacing"].get<double>(),
                                       settings["edgePadding"].get<double>());
          tracker.setZeroingOffsets(settings.value("zeroingX", 0.0),
                                    settings.value("zeroingY", 0.0));
          
          // 載入新環境的射手歸零值
          loadCurrentShooterZeroing(tracker);
          
          T91::IRTracker::log("Environment switched to: " +
                              getEnvironmentKey());
        } else if (action == "adjust_targets") {
          // 更新當前環境的靶板調整參數
          std::string key = getEnvironmentKey();
          if (!allTargetSettings.contains(key)) {
            allTargetSettings[key] = getDefaultTargetSettings();
          }

          if (data.contains("scale"))
            allTargetSettings[key]["scale"] = data["scale"];
          if (data.contains("vertical"))
            allTargetSettings[key]["vertical"] = data["vertical"];
          if (data.contains("spacing"))
            allTargetSettings[key]["spacing"] = data["spacing"];
          if (data.contains("edgePadding"))
            allTargetSettings[key]["edgePadding"] = data["edgePadding"];
          if (data.contains("zeroingX"))
            allTargetSettings[key]["zeroingX"] = data["zeroingX"];
          if (data.contains("zeroingY"))
            allTargetSettings[key]["zeroingY"] = data["zeroingY"];

          // 同步到 tracker 的命中檢測邏輯
          json settings = allTargetSettings[key];
          tracker.setTargetAdjustments(settings["scale"].get<double>(),
                                       settings["vertical"].get<double>(),
                                       settings["spacing"].get<double>(),
                                       settings["edgePadding"].get<double>());
          tracker.setZeroingOffsets(settings.value("zeroingX", 0.0),
                                    settings.value("zeroingY", 0.0));

          // 自動儲存到檔案（配合前端自動儲存機制）
          saveAllTargetSettings();

          T91::IRTracker::log("Target adjustments updated and saved for: " + key);
        } else if (action == "save_target_settings") {
          // 儲存所有環境的靶板設定到 JSON 檔案
          saveAllTargetSettings();
        } else if (action == "knockdown_all") {
          tracker.knockdownAll();
          T91::IRTracker::log("All targets knocked down");
        } else if (action == "reset_all") {
          tracker.resetAll();
          T91::IRTracker::log("All targets reset to standing");
        } else if (action == "knockdown_target") {
          int shooterId = data.value("shooter_id", 1);
          tracker.knockdownTarget(shooterId);
          T91::IRTracker::log("Target " + std::to_string(shooterId) +
                              " knocked down");
        } else if (action == "reset_target") {
          int shooterId = data.value("shooter_id", 1);
          tracker.resetTarget(shooterId);
          T91::IRTracker::log("Target " + std::to_string(shooterId) +
                              " standing");
        } else if (action == "adjust_global_zeroing") {
          double zx = data["zeroingX"].get<double>();
          double zy = data["zeroingY"].get<double>();

          if (!allTargetSettings.contains("global")) {
            allTargetSettings["global"] = {{"zeroingX", 0.0},
                                           {"zeroingY", 0.0}};
          }
          if (data.contains("zeroingX"))
            allTargetSettings["global"]["zeroingX"] = data["zeroingX"];
          if (data.contains("zeroingY"))
            allTargetSettings["global"]["zeroingY"] = data["zeroingY"];

          tracker.setGlobalZeroingOffsets(zx, zy);

          // 存檔
          allTargetSettings["global"]["zeroingX"] = zx;
          allTargetSettings["global"]["zeroingY"] = zy;
          saveAllTargetSettings();

          res.set_content(json({{"status", "ok"}}).dump(), "application/json");
        } else if (action == "adjust_shooter_zeroing") {
          int id = data["shooterId"].get<int>();
          double zx = data["zeroingX"].get<double>();
          double zy = data["zeroingY"].get<double>();
          tracker.setShooterZeroing(id, zx, zy);

          // 存到當前環境的射手設定（按距離分開）
          std::string key = getEnvironmentKey();
          if (!allTargetSettings.contains(key)) {
            allTargetSettings[key] = getDefaultTargetSettings();
          }
          if (!allTargetSettings[key].contains("shooters")) {
            allTargetSettings[key]["shooters"] = getDefaultShooterSettings();
          }

          bool found = false;
          for (auto &s : allTargetSettings[key]["shooters"]) {
            if (s["id"] == id) {
              s["zeroingX"] = zx;
              s["zeroingY"] = zy;
              found = true;
              break;
            }
          }
          if (!found) {
            allTargetSettings[key]["shooters"].push_back(
                {{"id", id}, {"zeroingX", zx}, {"zeroingY", zy}});
          }
          
          saveAllTargetSettings();
          T91::IRTracker::log("Shooter " + std::to_string(id) + 
                              " zeroing saved for: " + key);
          res.set_content(json({{"status", "ok"}}).dump(), "application/json");
        } else if (action == "adjust_shooter_intensity") {
          int id = data["shooterId"].get<int>();
          int intensity = data["intensity"].get<int>();
          tracker.setShooterIntensity(id, intensity);

          // 強度特徵存在全域（不隨距離變化）
          if (!allTargetSettings.contains("shooters_intensity") ||
              !allTargetSettings["shooters_intensity"].is_array()) {
            allTargetSettings["shooters_intensity"] = json::array();
            for (int i = 1; i <= 6; ++i)
              allTargetSettings["shooters_intensity"].push_back(
                  {{"id", i}, {"intensity", 0}, {"minIntensity", 0}});
          }

          bool found = false;
          for (auto &s : allTargetSettings["shooters_intensity"]) {
            if (s["id"] == id) {
              s["intensity"] = intensity;
              found = true;
              break;
            }
          }
          if (!found) {
            allTargetSettings["shooters_intensity"].push_back(
                {{"id", id}, {"intensity", intensity}, {"minIntensity", 0}});
          }
          saveAllTargetSettings();
          res.set_content(json({{"status", "ok"}}).dump(), "application/json");
        } else if (action == "enable_intensity_id") {
          bool enabled = data["enabled"].get<bool>();
          tracker.setIntensityIdEnabled(enabled);
          allTargetSettings["useIntensityId"] = enabled;
          saveAllTargetSettings();
          res.set_content(json({{"status", "ok"}}).dump(), "application/json");
        } else if (action == "set_shooter_min_intensity") {
          int id = data["shooterId"].get<int>();
          int minIntensity = data["minIntensity"].get<int>();
          tracker.setShooterMinIntensity(id, minIntensity);

          // 最低強度門檻存在全域（不隨距離變化）
          if (!allTargetSettings.contains("shooters_intensity") ||
              !allTargetSettings["shooters_intensity"].is_array()) {
            allTargetSettings["shooters_intensity"] = json::array();
            for (int i = 1; i <= 6; ++i)
              allTargetSettings["shooters_intensity"].push_back(
                  {{"id", i}, {"intensity", 0}, {"minIntensity", 0}});
          }

          bool found = false;
          for (auto &s : allTargetSettings["shooters_intensity"]) {
            if (s["id"] == id) {
              s["minIntensity"] = minIntensity;
              found = true;
              break;
            }
          }
          if (!found) {
            allTargetSettings["shooters_intensity"].push_back(
                {{"id", id}, {"intensity", 0}, {"minIntensity", minIntensity}});
          }
          saveAllTargetSettings();
          res.set_content(json({{"status", "ok"}}).dump(), "application/json");
        } 
        // 功能 B4：自動校準端點
        else if (action == "start_auto_calibration_a") {
          tracker.startAutoCalibrationA();
          res.set_content(json({{"status", "ok"}}).dump(), "application/json");
        } else if (action == "start_auto_calibration_b") {
          tracker.startAutoCalibrationB();
          res.set_content(json({{"status", "ok"}}).dump(), "application/json");
        } else if (action == "get_calibration_status") {
          json calib_status = tracker.getCalibrationStatus();
          res.set_content(json({{"status", "ok"}, {"data", calib_status}}).dump(), "application/json");
        }
        else if (action == "shutdown_system") {
          std::cout << "\n[SYSTEM] Shutdown command received. Cleaning up...\n"
                    << std::endl;
          res.set_content(json({{"status", "ok"}}).dump(), "application/json");

          // 使用全域標記來觸發伺服器關閉
          std::thread([]() {
            // 等待回應發送完成
            std::this_thread::sleep_for(std::chrono::milliseconds(100));

            std::cout << "[SYSTEM] Terminating Chrome processes..."
                      << std::endl;
            // 呼叫系統 taskkill，使用完整路徑確保執行
            system("C:\\Windows\\System32\\taskkill.exe /F /IM chrome.exe /T "
                   ">nul 2>&1");

            std::cout
                << "[SYSTEM] Chrome terminated. Exiting backend immediately..."
                << std::endl;
            // 直接結束後端程式。外部的 PowerShell
            // 腳本偵測到後端結束後，會自動卸載 X: 磁碟並清理
            std::_Exit(0);
          }).detach();
        } else {
          res.status = 400;
          res.set_content(
              json{{"status", "error"}, {"message", "Unknown action"}}.dump(),
              "application/json");
        }
      } catch (const std::exception &e) {
        res.status = 400;
        res.set_content(json{{"status", "error"}, {"message", e.what()}}.dump(),
                        "application/json");
      }
    });

    svr.Get("/shots", [&](const httplib::Request &req, httplib::Response &res) {
      res.set_content(tracker.getShots().dump(), "application/json");
    });

    svr.Get("/state", [&](const httplib::Request &req, httplib::Response &res) {
      json state = tracker.getState();
      // 添加當前環境的靶板調整參數到狀態
      state["targetAdjustments"] = getCurrentTargetSettings();
      state["environment"] = getEnvironmentKey();
      res.set_content(state.dump(), "application/json");
    });

    T91::IRTracker::log("HTTP Server starting at port 8081...");
    if (!svr.listen("0.0.0.0", 8081)) {
      T91::IRTracker::log("FATAL: Port 8081 is busy.");
      tracker.stop();
      return 1;
    }

    tracker.stop();
  } catch (const std::exception &e) {
    T91::IRTracker::log("CRITICAL EXCEPTION: " + std::string(e.what()));
    return 1;
  } catch (...) {
    T91::IRTracker::log("CRITICAL: Unknown fatal error.");
    return 1;
  }
  return 0;
}
