# 每次构建时由 kleinbot_refresh_version 目标调用（configure 阶段也先执行一次），
# 重新生成 generated/KleinVersion.h，供 --version 输出、启动日志与帮助文本消费。
#
# 参数（全部由调用方 -D 传入）：
#   KLEINBOT_TEMPLATE      KleinVersion.h.in 模板路径
#   KLEINBOT_HEADER_OUT    生成的头文件路径
#   KLEINBOT_SOURCE_DIR    git 仓库根目录
#   GIT_EXECUTABLE         git 可执行文件（找不到时传空，哈希回退 unknown）
#   KLEINBOT_VERSION / KLEINBOT_VERSION_SUFFIX / KLEINBOT_ARCH /
#   KLEINBOT_BUILD_TYPE / KLEINBOT_COMPILER   模板占位符的值
#
# configure_file 只在内容变化时更新文件时间戳，因此哈希未变不会触发重编。

set(KLEINBOT_GIT_HASH "unknown")
if(GIT_EXECUTABLE)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" -C "${KLEINBOT_SOURCE_DIR}" rev-parse --short HEAD
        OUTPUT_VARIABLE kleinbot_git_short_hash
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE kleinbot_git_result
    )
    if(kleinbot_git_result EQUAL 0 AND NOT kleinbot_git_short_hash STREQUAL "")
        set(KLEINBOT_GIT_HASH "${kleinbot_git_short_hash}")
        # 只检测已跟踪文件，未跟踪文件（如构建产物）不影响 dirty 判定
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" -C "${KLEINBOT_SOURCE_DIR}" diff-index --quiet HEAD --
            RESULT_VARIABLE kleinbot_dirty_result
        )
        if(NOT kleinbot_dirty_result EQUAL 0)
            set(KLEINBOT_GIT_HASH "${kleinbot_git_short_hash}-dirty")
        endif()
    endif()
endif()

configure_file("${KLEINBOT_TEMPLATE}" "${KLEINBOT_HEADER_OUT}" @ONLY)
