#ifndef VISIONRESPONSE_H
#define VISIONRESPONSE_H

#include <string>

struct VisionResponse
{
    int code = 0;
    std::string content;           // 图片识别描述
    std::string refusal;           // 拒绝时的理由（content_filter 等场景）
    std::string finish_reason;

    int input_tokens = 0;
    int output_tokens = 0;
    int total_tokens = 0;

    std::string error_message;
};

#endif // VISIONRESPONSE_H
