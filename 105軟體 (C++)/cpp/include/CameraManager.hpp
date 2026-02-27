#pragma once

#ifdef USE_PYLON
#ifndef _stdcall
#define _stdcall __stdcall
#endif

#include <stdint.h>
#ifndef _STDINT
#define _STDINT // Prevent Pylon headers from re-defining int8_t etc.
#endif

// Guard against macro collisions from windows.h
#define NOMINMAX
#include <windows.h>

// Some Windows/X11 headers might define 'Status' as a macro. Undefine it to allow Pylon struct access.
#ifdef Status
#undef Status
#endif

#ifndef PYLON_WIN_BUILD
#define PYLON_WIN_BUILD
#endif
#ifndef GENAPIC_WIN_BUILD
#define GENAPIC_WIN_BUILD
#endif

#include <pylonc/PylonC.h>
#endif

#include <opencv2/core.hpp>
#include <opencv2/videoio.hpp>
#include <opencv2/highgui.hpp>
#include <string>

namespace T91 {

class ICamera {
public:
    virtual ~ICamera() = default;
    virtual bool open() = 0;
    virtual bool grab(cv::Mat& frame) = 0;
    virtual void close() = 0;
    virtual bool isOpened() const = 0;
};

#ifdef USE_PYLON
class BaslerCamera : public ICamera {
public:
    BaslerCamera(int64_t serial);
    bool open() override;
    bool grab(cv::Mat& frame) override;
    void close() override;
    bool isOpened() const override;
private:
    int64_t serial_;
    int index_ = -1;
    int width_ = 0;
    int height_ = 0;
    PYLON_DEVICE_HANDLE hDev_ = NULL;
    PYLON_STREAMGRABBER_HANDLE hGrabber_ = NULL;
    PYLON_WAITOBJECT_HANDLE hWait_ = NULL;
    unsigned char* pBuffer_ = nullptr;
    size_t payloadSize_ = 0;
    PYLON_STREAMBUFFER_HANDLE hBuf_ = NULL;
};
#endif

class MockCamera : public ICamera {
public:
    MockCamera(int index);
    bool open() override;
    bool grab(cv::Mat& frame) override;
    void close() override;
    bool isOpened() const override;
private:
    int index_;
    cv::VideoCapture cap_;
};

} // namespace T91
