#ifndef NTT_LOG_H
#define NTT_LOG_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Logging severity levels.
 */
typedef enum {
    NTT_LOG_NONE = 0,  /* logging disabled */
    NTT_LOG_ERROR = 1, /* unrecoverable failures */
    NTT_LOG_INFO = 2,  /* context lifecycle */
    NTT_LOG_DEBUG = 3, /* per-call tracing debug */
} ntt_log_level;

/**
 * @brief Sets the runtime logging threshold.
 *
 * Messages with a severity than the configured level are discarded.
 *
 * @param[in] level Logging severity threshold.
 *
 * @note The default logging level is NTT_LOG_ERROR.
 *
 * @see ntt_log_level
 */
void ntt_log_set_level(ntt_log_level level);

/**
 * Internal logging function. This function should not be called directly. 
 * Use the NTT_LOG() macro instead 
 */
void ntt__log_write(ntt_log_level level,
                    const char *file,
                    int line,
                    const char *fmt,
                    ...);

/**
 * NTT_LOG_ENABLED is a compile-time option (set via CMake option
 * NTT_ENABLE_LOGGING). When it's off, NTT_LOG() expands to nothing, so it 
 * costs literally zero at runtime and is safe to leave scattered through the 
 * code, including near hot paths, for release or benchmark builds.
 */
#if defined(NTT_LOG_ENABLED)
#define NTT_LOG(level, ...) \
    ntt__log_write((level), __FILE__, __LINE__, __func__, __VA_ARGS__)
#else
#define NTT_LOG(level, ...) \
    do {                    \
    } while (0)
#endif

#ifdef __cplusplus
}
#endif

#endif /* NTT_LOG_H */
