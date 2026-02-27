#pragma once

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <vector>
#include <iostream>
#include <chrono>

namespace T91 {

struct Point2D {
    double x;
    double y;
    int area;
};

class Detector {
public:
    Detector(int threshold = 200, int min_area = 3, int max_area = 5000)
        : threshold_(threshold), min_area_(min_area), max_area_(max_area) {
        last_log_time_ = std::chrono::steady_clock::now();
    }

    std::vector<Point2D> detect(const cv::Mat& frame) {
        cv::Mat gray, thresh;
        if (frame.channels() == 3) {
            cv::cvtColor(frame, gray, cv::COLOR_BGR2GRAY);
        } else {
            gray = frame;
        }

        cv::threshold(gray, thresh, (double)threshold_, 255, cv::THRESH_BINARY);

        double maxVal = 0;
        cv::minMaxLoc(gray, NULL, &maxVal);

        std::vector<std::vector<cv::Point>> contours;
        cv::findContours(thresh, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

        std::vector<Point2D> points;
        for (const auto& contour : contours) {
            double area = cv::contourArea(contour);
            if (area < min_area_ || area > max_area_) continue;

            cv::Moments m = cv::moments(contour);
            if (m.m00 == 0) continue;

            double cx = m.m10 / m.m00;
            double cy = m.m01 / m.m00;
            points.push_back({cx, cy, static_cast<int>(area)});
        }

        // 診斷輸出：每秒印一次狀態
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - last_log_time_).count() >= 1000) {
            std::cout << "[DEBUG] Max pixel: " << (int)maxVal << " | Thresh: " << threshold_ << " | Points found: " << points.size() << std::endl << std::flush;
            last_log_time_ = now;
        }

        return points;
    }

    void setThreshold(int t) { threshold_ = t; }

private:
    int threshold_;
    int min_area_;
    int max_area_;
    std::chrono::steady_clock::time_point last_log_time_;
};

} // namespace T91
