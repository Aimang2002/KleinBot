#ifndef IMAGERESPONSE_H
#define IMAGERESPONSE_H

#include <string>

struct ImageResponse
{
    bool cancelled = false;
    int code = 0;
    std::string image_base64;      // 生成图片的 base64 编码
    std::string image_url;         // 接口只给了 url 时（部分中转站默认形态）的下载地址

    int input_tokens = 0;
    int output_tokens = 0;
    int total_tokens = 0;

    std::string error_message;
};

#endif // IMAGERESPONSE_H
