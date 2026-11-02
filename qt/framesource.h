#pragma once
#include <opencv2/core.hpp>

// 프레임 공급자 인터페이스.
// DisplayThread 가 CaptureThread 에 직접 묶이지 않게 하려고 분리했다.
class FrameSource
{
public:
    virtual ~FrameSource() = default;
    virtual cv::Mat readLatest() const = 0;   // 없으면 empty
};
