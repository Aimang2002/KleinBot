#ifndef VOICE_ATTACHMENT_CLEANUP_H
#define VOICE_ATTACHMENT_CLEANUP_H

#include "../Port/OutboundMessage.h"

#include <filesystem>
#include <optional>
#include <string>

// 语音附件是系统临时目录里的过路文件：投递一旦离开发送队列（成功、失败或会话终结），
// 队列无重试、文件再无用途，由 RAII 删除防止临时目录累积。
// 同机部署的前置下主流 OneBot 实现（NapCat/Lagrange 等）在处理动作时同步读取
// record 文件，发送返回后立即删除是安全的
class VoiceAttachmentCleanup
{
public:
    VoiceAttachmentCleanup() = default;
    explicit VoiceAttachmentCleanup(std::optional<std::string> path) : path(std::move(path)) {}

    void hold(std::optional<std::string> attachmentPath) { path = std::move(attachmentPath); }

    void cleanup()
    {
        if (!path)
            return;
        std::error_code ignoredError;
        std::filesystem::remove(*path, ignoredError);
        path.reset();
    }

    ~VoiceAttachmentCleanup() { cleanup(); }

private:
    std::optional<std::string> path;
};

#endif // VOICE_ATTACHMENT_CLEANUP_H
