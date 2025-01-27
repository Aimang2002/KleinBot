#include "CloudMusicID.h"

size_t WriteCallback(void *contents, size_t size, size_t nmemb, std::string *s)
{
    size_t newLength = size * nmemb;
    try
    {
        s->append((char *)contents, newLength);
    }
    catch (std::bad_alloc &e)
    {
        return 0;
    }
    return newLength;
}

std::string CloudMusicID::urlEncode(const std::string &url)
{
    std::ostringstream encoded;
    for (char c : url)
    {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~' ||
            c == ':' || c == '/' || c == '?' || c == '=' || c == '&')
        {
            encoded << c;
        }
        else
        {
            // 对非ASCII字符进行编码
            encoded << '%' << std::uppercase << std::hex << (int)(unsigned char)c;
        }
    }
    return encoded.str();
}

std::string CloudMusicID::searchSong(const std::string songName)
{
    CURL *curl;
    CURLcode res;
    std::string readBuffer;

    curl = curl_easy_init();
    if (curl)
    {
        std::string url = "https://music.163.com/api/search/get/web";
        std::string params = "s=" + songName + "&type=1&limit=1";
        std::string request = urlEncode(url + "?" + params);

#if defined(__WIN32) || defined(__WIN64)
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
        curl_easy_setopt(curl, CURLOPT_CAINFO, "cacert.pem");
#endif
        curl_easy_setopt(curl, CURLOPT_URL, request.c_str());

        struct curl_slist *headers = NULL;
        headers = curl_slist_append(headers, "User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/58.0.3029.110 Safari/537.3");
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);

        res = curl_easy_perform(curl);

        curl_easy_cleanup(curl);
        curl_slist_free_all(headers);

        // 检查请求是否成功
        if (res == CURLE_OK)
        {
            size_t res = readBuffer.find(R"("id":)");
            if (res != std::string::npos)
            {
                res += 5;
                return readBuffer.substr(res, readBuffer.find(",") - res);
            }
        }
    }
    LOG_ERROR("获取网易云ID失败!");
    return "";
}

CloudMusicID::CloudMusicID()
{
}

CloudMusicID::~CloudMusicID()
{
}