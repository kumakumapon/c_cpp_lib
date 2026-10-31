#include "embed_log/embed_log.h"
#include <stdio.h>
#include <string.h>

/* ANSIカラーエスケープシーケンス */
#define ANSI_COLOR_RESET "\x1b[0m"
#define ANSI_COLOR_RED "\x1b[31m"
#define ANSI_COLOR_GREEN "\x1b[32m"
#define ANSI_COLOR_YELLOW "\x1b[33m"
#define ANSI_COLOR_BLUE "\x1b[34m"
#define ANSI_COLOR_MAGENTA "\x1b[35m"
#define ANSI_COLOR_CYAN "\x1b[36m"
#define ANSI_COLOR_GRAY "\x1b[90m"

typedef struct {
    embed_log_output_fn fn;
    void *user_data;
} backend_entry_t;

typedef struct {
    embed_log_level_t level;
    bool color_enabled;
    backend_entry_t backends[EMBED_LOG_MAX_BACKENDS];
    size_t backend_count;

    embed_log_get_time_fn time_fn;

    embed_log_lock_fn lock;
    embed_log_unlock_fn unlock;
    void *lock_user_data;
} embed_logger_t;

static embed_logger_t s_logger;

static const char *level_to_str(embed_log_level_t level) {
    switch (level) {
    case EMBED_LOG_LEVEL_FATAL:
        return "FATAL";
    case EMBED_LOG_LEVEL_ERROR:
        return "ERROR";
    case EMBED_LOG_LEVEL_WARN:
        return "WARN ";
    case EMBED_LOG_LEVEL_INFO:
        return "INFO ";
    case EMBED_LOG_LEVEL_DEBUG:
        return "DEBUG";
    case EMBED_LOG_LEVEL_TRACE:
        return "TRACE";
    default:
        return "NONE ";
    }
}

static const char *level_to_color(embed_log_level_t level) {
    switch (level) {
    case EMBED_LOG_LEVEL_FATAL:
        return ANSI_COLOR_MAGENTA;
    case EMBED_LOG_LEVEL_ERROR:
        return ANSI_COLOR_RED;
    case EMBED_LOG_LEVEL_WARN:
        return ANSI_COLOR_YELLOW;
    case EMBED_LOG_LEVEL_INFO:
        return ANSI_COLOR_GREEN;
    case EMBED_LOG_LEVEL_DEBUG:
        return ANSI_COLOR_CYAN;
    case EMBED_LOG_LEVEL_TRACE:
        return ANSI_COLOR_GRAY;
    default:
        return ANSI_COLOR_RESET;
    }
}

static inline void logger_enter_critical(void) {
    if (s_logger.lock) {
        s_logger.lock(s_logger.lock_user_data);
    }
}

static inline void logger_exit_critical(void) {
    if (s_logger.unlock) {
        s_logger.unlock(s_logger.lock_user_data);
    }
}

static void default_stdout_backend(const char *str, size_t length, void *user_data) {
    (void)user_data;
    (void)length;
    fputs(str, stdout);
    fflush(stdout);
}

void embed_log_init(void) {
    memset(&s_logger, 0, sizeof(s_logger));
    s_logger.level = EMBED_LOG_LEVEL_DEBUG;
    s_logger.color_enabled = true;
    s_logger.backend_count = 0;

    /* デフォルトでstdoutをバックエンドに登録 */
    embed_log_add_backend(default_stdout_backend, NULL);
}

bool embed_log_add_backend(embed_log_output_fn output_fn, void *user_data) {
    if (!output_fn || s_logger.backend_count >= EMBED_LOG_MAX_BACKENDS) {
        return false;
    }

    logger_enter_critical();
    s_logger.backends[s_logger.backend_count].fn = output_fn;
    s_logger.backends[s_logger.backend_count].user_data = user_data;
    s_logger.backend_count++;
    logger_exit_critical();
    return true;
}

void embed_log_clear_backends(void) {
    logger_enter_critical();
    s_logger.backend_count = 0;
    logger_exit_critical();
}

void embed_log_set_level(embed_log_level_t level) { s_logger.level = level; }

embed_log_level_t embed_log_get_level(void) { return s_logger.level; }

void embed_log_set_timestamp_fn(embed_log_get_time_fn time_fn) { s_logger.time_fn = time_fn; }

void embed_log_set_color_enabled(bool enabled) { s_logger.color_enabled = enabled; }

void embed_log_set_lock(embed_log_lock_fn lock, embed_log_unlock_fn unlock, void *user_data) {
    s_logger.lock = lock;
    s_logger.unlock = unlock;
    s_logger.lock_user_data = user_data;
}

static const char *get_filename_only(const char *path) {
    if (!path)
        return "";
    const char *last = path;
    while (*path) {
        if (*path == '/' || *path == '\\') {
            last = path + 1;
        }
        path++;
    }
    return last;
}

static void dispatch_to_backends(const char *str, size_t len) {
    for (size_t i = 0; i < s_logger.backend_count; ++i) {
        if (s_logger.backends[i].fn) {
            s_logger.backends[i].fn(str, len, s_logger.backends[i].user_data);
        }
    }
}

/* Clamp each append before doing pointer arithmetic; printf reports the length
 * that would have been written, or a negative value on encoding failure. */
static size_t append_va(char *buffer, size_t cap, size_t offset, const char *fmt, va_list args) {
    if (!fmt || offset >= cap - 1)
        return offset;
    int n = vsnprintf(buffer + offset, cap - offset, fmt, args);
    if (n < 0) {
        buffer[offset] = '\0';
        return offset;
    }
    return (size_t)n >= cap - offset ? cap - 1 : offset + (size_t)n;
}
static size_t append(char *buffer, size_t cap, size_t offset, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    size_t result = append_va(buffer, cap, offset, fmt, args);
    va_end(args);
    return result;
}
void embed_log_write_va(embed_log_level_t level, const char *tag, const char *file, int line,
                        const char *fmt, va_list args) {
    if (level > s_logger.level || level == EMBED_LOG_LEVEL_NONE || !fmt)
        return;
    char buffer[EMBED_LOG_MAX_LINE_LENGTH] = {0};
    logger_enter_critical();
    uint32_t ts = s_logger.time_fn ? s_logger.time_fn() : 0;
    /* 長い行を切り詰めても、末尾の色リセットと改行は必ず残す。本体はその分を除いた容量に収める */
    const char *suffix = s_logger.color_enabled ? ANSI_COLOR_RESET : "";
    const size_t body_cap = sizeof(buffer) - (strlen(suffix) + 1);
    size_t offset = append(buffer, body_cap, 0, "%s[%08u][%s][%s] ",
                           s_logger.color_enabled ? level_to_color(level) : "", (unsigned)ts,
                           level_to_str(level), tag ? tag : "SYS");
    offset = append_va(buffer, body_cap, offset, fmt, args);
    if (level >= EMBED_LOG_LEVEL_DEBUG)
        offset = append(buffer, body_cap, offset, " (%s:%d)", get_filename_only(file), line);
    offset = append(buffer, sizeof(buffer), offset, "%s\n", suffix);
    dispatch_to_backends(buffer, offset);
    logger_exit_critical();
}

void embed_log_write(embed_log_level_t level, const char *tag, const char *file, int line,
                     const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    embed_log_write_va(level, tag, file, line, fmt, args);
    va_end(args);
}

void embed_log_hexdump(embed_log_level_t level, const char *tag, const void *data, size_t size) {
    if (level == EMBED_LOG_LEVEL_NONE || level > s_logger.level || !data || size == 0) {
        return;
    }

    const uint8_t *bytes = (const uint8_t *)data;
    char line_buf[128];

    embed_log_write(level, tag, "", 0, "--- HexDump (%zu bytes) ---", size);

    logger_enter_critical();

    for (size_t i = 0; i < size; i += 16) {
        int offset = snprintf(line_buf, sizeof(line_buf), "  %04X: ", (unsigned int)i);

        // Hex表示
        for (size_t j = 0; j < 16; ++j) {
            if (i + j < size) {
                offset +=
                    snprintf(line_buf + offset, sizeof(line_buf) - offset, "%02X ", bytes[i + j]);
            } else {
                offset += snprintf(line_buf + offset, sizeof(line_buf) - offset, "   ");
            }
        }

        offset += snprintf(line_buf + offset, sizeof(line_buf) - offset, " |");

        // ASCII表示
        for (size_t j = 0; j < 16 && (i + j) < size; ++j) {
            uint8_t c = bytes[i + j];
            char printable = (c >= 32 && c <= 126) ? (char)c : '.';
            if (offset < (int)sizeof(line_buf) - 1) {
                line_buf[offset++] = printable;
            }
        }
        if (offset < (int)sizeof(line_buf) - 2) {
            line_buf[offset++] = '|';
            line_buf[offset++] = '\n';
            line_buf[offset] = '\0';
        }

        line_buf[offset] = '\0';
        dispatch_to_backends(line_buf, (size_t)offset);
    }

    logger_exit_critical();
}
