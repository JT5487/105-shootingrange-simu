#include "CameraManager.hpp"
#include <iostream>
#include <iomanip>
#include <malloc.h>
#include <cstdint>
#include <thread>
#include <chrono>

namespace T91 {

MockCamera::MockCamera(int index) : index_(index) {}

bool MockCamera::open() {
    cap_.open(index_);
    if (!cap_.isOpened()) {
        std::cout << "MockCamera " << index_ << " failed to open real camera, using blank frames." << std::endl;
        return true; 
    }
    return true;
}

bool MockCamera::grab(cv::Mat& frame) {
    if (cap_.isOpened()) {
        return cap_.read(frame);
    } else {
        frame = cv::Mat::zeros(480, 640, CV_8UC1);
        return true;
    }
}

void MockCamera::close() {
    cap_.release();
}

bool MockCamera::isOpened() const {
    return true;
}

#ifdef USE_PYLON
BaslerCamera::BaslerCamera(int64_t serial) : serial_(serial), index_(-1), width_(0), height_(0) {}

bool BaslerCamera::open() {
    std::cout << "[INFO] Attempting to open Basler Camera S/N: " << serial_ << "..." << std::endl;
    if (PylonInitialize() != GENAPI_E_OK) return false;
    
    // GigE 相機可能需要較長時間進行網路掃描，特別是共享交換器時
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    // 重試最多 3 次來尋找相機
    int maxRetries = 3;
    for (int retry = 0; retry < maxRetries; ++retry) {
        size_t numDevices;
        if (PylonEnumerateDevices(&numDevices) != GENAPI_E_OK) {
            std::cerr << "PylonEnumerateDevices failed" << std::endl;
            return false;
        }

        std::cout << "[DEBUG] Pylon found " << numDevices << " devices (attempt " << (retry + 1) << "/" << maxRetries << ")." << std::endl;
        index_ = -1;
        for (size_t i = 0; i < numDevices; ++i) {
            PYLON_DEVICE_INFO_HANDLE hDi;
            if (PylonGetDeviceInfoHandle(i, &hDi) == GENAPI_E_OK) {
                char serialStr[256] = {0};
                size_t len = 256;
                PylonDeviceInfoGetPropertyValueByName(hDi, "SerialNumber", serialStr, &len);
                
                if (std::string(serialStr) == std::to_string(serial_)) {
                    index_ = (int)i;
                    std::cout << "  [SUCCESS] Match found at index " << index_ << std::endl;
                    break;
                }
            }
        }

        if (index_ != -1) break;  // 找到了，跳出重試迴圈
        
        std::cout << "  [RETRY] Camera S/N " << serial_ << " not found, waiting..." << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }

    if (index_ == -1) {
        std::cerr << "[ERROR] Could not find Basler Camera with S/N: " << serial_ << std::endl;
        return false;
    }

    GENAPIC_RESULT res;
    if ((res = PylonCreateDeviceByIndex((size_t)index_, &hDev_)) != GENAPI_E_OK) {
        std::cerr << "PylonCreateDeviceByIndex failed: 0x" << std::hex << (unsigned int)res << std::dec << std::endl;
        return false;
    }

    if ((res = PylonDeviceOpen(hDev_, PYLONC_ACCESS_MODE_CONTROL | PYLONC_ACCESS_MODE_STREAM)) != GENAPI_E_OK) {
        std::cerr << "PylonDeviceOpen failed: 0x" << std::hex << (unsigned int)res << std::dec << std::endl;
        return false;
    }

    // 使用 Pylon 官方 C API 功能函數設定參數
    int64_t w, h;
    PylonDeviceGetIntegerFeature(hDev_, "Width", &w);
    PylonDeviceGetIntegerFeature(hDev_, "Height", &h);
    width_ = (int)w;
    height_ = (int)h;
    std::cout << "[DEBUG] Camera Resolution: " << width_ << "x" << height_ << std::endl;

    // 使用 PylonViewer 實測驗證的最佳設定
    PylonDeviceSetFloatFeature(hDev_, "ExposureTime", 8000.0);  // 用戶測試確認
    // GainRaw = 808 (用戶在 PylonViewer 測試可清晰看到雷射)
    if (PylonDeviceSetIntegerFeature(hDev_, "GainRaw", 808) != GENAPI_E_OK) {
        PylonDeviceSetFloatFeature(hDev_, "Gain", 5.0);  // 備用中低增益
    }
    PylonDeviceFeatureFromString(hDev_, "PixelFormat", "Mono8");
    
    // 降低幀率到 45fps 減少 GigE 網路頻寬使用（兩台相機共用一個網路埠時）
    PylonDeviceFeatureFromString(hDev_, "AcquisitionFrameRateEnable", "1");
    PylonDeviceSetFloatFeature(hDev_, "AcquisitionFrameRate", 45.0);
    std::cout << "[DEBUG] ExposureTime=8000, GainRaw=808, FrameRate=45fps applied." << std::endl;

    if ((res = PylonDeviceGetStreamGrabber(hDev_, 0, &hGrabber_)) != GENAPI_E_OK) return false;
    if ((res = PylonStreamGrabberOpen(hGrabber_)) != GENAPI_E_OK) return false;
    if ((res = PylonStreamGrabberGetPayloadSize(hDev_, hGrabber_, &payloadSize_)) != GENAPI_E_OK) return false;
    
    PylonStreamGrabberPrepareGrab(hGrabber_);

    pBuffer_ = (unsigned char*)_aligned_malloc(payloadSize_, 16);
    if (!pBuffer_) return false;

    PylonStreamGrabberRegisterBuffer(hGrabber_, pBuffer_, payloadSize_, &hBuf_);
    PylonStreamGrabberQueueBuffer(hGrabber_, hBuf_, NULL);
    
    PylonDeviceExecuteCommandFeature(hDev_, "AcquisitionStart");
    PylonStreamGrabberGetWaitObject(hGrabber_, &hWait_);

    std::cout << "[SUCCESS] Camera S/N " << serial_ << " started streaming." << std::endl;
    return true;
}

bool BaslerCamera::grab(cv::Mat& frame) {
    if (hGrabber_ == NULL || width_ <= 0 || height_ <= 0) return false;

    PylonGrabResult_t result;
    _Bool isReady;
    
    if (PylonWaitObjectWait(hWait_, 200, &isReady) == GENAPI_E_OK && isReady) {
        if (PylonStreamGrabberRetrieveResult(hGrabber_, &result, &isReady) == GENAPI_E_OK && isReady) {
            if (result.ErrorCode == 0) { 
                frame = cv::Mat(height_, width_, CV_8UC1, (void*)result.pBuffer).clone();
                
                static int grabCount = 0;
                if (++grabCount % 100 == 0) {
                     std::cout << "[DEBUG] Camera S/N: " << serial_ << " grab " << grabCount << " OK." << std::endl;
                }

                PylonStreamGrabberQueueBuffer(hGrabber_, result.hBuffer, NULL);
                return true;
            } else {
                std::cerr << "[DEBUG] Grab S/N " << serial_ << " error: 0x" << std::hex << result.ErrorCode << std::dec << std::endl;
            }
            PylonStreamGrabberQueueBuffer(hGrabber_, result.hBuffer, NULL);
        }
    }
    return false;
}

void BaslerCamera::close() {
    if (hDev_ != NULL) {
        PylonDeviceExecuteCommandFeature(hDev_, "AcquisitionStop");
        PylonStreamGrabberClose(hGrabber_);
        PylonDeviceClose(hDev_);
        PylonDestroyDevice(hDev_);
        if (pBuffer_) _aligned_free(pBuffer_);
        hDev_ = NULL;
    }
}

bool BaslerCamera::isOpened() const {
    return hDev_ != NULL;
}
#endif

} // namespace T91
