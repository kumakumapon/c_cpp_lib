#pragma once
#include "embed_log.h"

#include <sstream>
#include <string>
#include <string_view>

namespace embed_log {

/**
 * @brief C++ ストリームスタイルのロギングヘルパークラス
 */
class LogStream {
public:
    LogStream(embed_log_level_t level, const char *tag, const char *file, int line)
        : level_(level), tag_(tag), file_(file), line_(line), enabled_(true) {
        if (level > embed_log_get_level()) {
            enabled_ = false;
        }
    }

    ~LogStream() {
        if (enabled_) {
            std::string str = oss_.str();
            embed_log_write(level_, tag_, file_, line_, "%s", str.c_str());
        }
    }

    template <typename T>
    LogStream &operator<<(const T &val) {
        if (enabled_) {
            oss_ << val;
        }
        return *this;
    }

private:
    embed_log_level_t level_;
    const char *tag_;
    const char *file_;
    int line_;
    bool enabled_;
    std::ostringstream oss_;
};

} // namespace embed_log

#define LOG_STREAM(level, tag) embed_log::LogStream(level, tag, __FILE__, __LINE__)
#define CPP_LOG_F(tag) LOG_STREAM(EMBED_LOG_LEVEL_FATAL, tag)
#define CPP_LOG_E(tag) LOG_STREAM(EMBED_LOG_LEVEL_ERROR, tag)
#define CPP_LOG_W(tag) LOG_STREAM(EMBED_LOG_LEVEL_WARN, tag)
#define CPP_LOG_I(tag) LOG_STREAM(EMBED_LOG_LEVEL_INFO, tag)
#define CPP_LOG_D(tag) LOG_STREAM(EMBED_LOG_LEVEL_DEBUG, tag)
#define CPP_LOG_T(tag) LOG_STREAM(EMBED_LOG_LEVEL_TRACE, tag)
