#include "ntt_log.c"
#include "test_common.h"

#ifndef _WIN32
#include <sys/types.h>
#include <unistd.h>
#endif

void torture_ntt_log_set_level(void **state)
{
    (void)state;

    ntt_log_set_level(NTT_LOG_NONE);
    assert_int_equal(atomic_load(&g_ntt_log_level), NTT_LOG_NONE);
    ntt_log_set_level(NTT_LOG_ERROR);
    assert_int_equal(atomic_load(&g_ntt_log_level), NTT_LOG_ERROR);
    ntt_log_set_level(NTT_LOG_INFO);
    assert_int_equal(atomic_load(&g_ntt_log_level), NTT_LOG_INFO);
    ntt_log_set_level(NTT_LOG_DEBUG);
    assert_int_equal(atomic_load(&g_ntt_log_level), NTT_LOG_DEBUG);
}

void torture_ntt_log_set_level_invalid(void **state)
{
    (void)state;
    ntt_log_set_level(NTT_LOG_DEBUG);

    /* Out-of-range values are discarded, leaving the level unchanged. */
    ntt_log_set_level((ntt_log_level)999);
    assert_int_equal(atomic_load(&g_ntt_log_level), NTT_LOG_DEBUG);
    ntt_log_set_level((ntt_log_level)-1);
    assert_int_equal(atomic_load(&g_ntt_log_level), NTT_LOG_DEBUG);
    ntt_log_set_level((ntt_log_level)0x7FFFFFFF);
    assert_int_equal(atomic_load(&g_ntt_log_level), NTT_LOG_DEBUG);
}

#ifndef _WIN32
/* Environment and stderr-redirection capture rely on POSIX APIs
 * (setenv/unsetenv, mkstemp, dup2, lseek, ...) that MSVC does not provide,
 * so those tests only run on POSIX platforms. */

void torture_ntt_log_env_level(void **state)
{
    (void)state;

    unsetenv("NTT_LOG_LEVEL");
    assert_int_equal(ntt_log_env_level(), NTT_LOG_ERROR);

    setenv("NTT_LOG_LEVEL", "none", 1);
    assert_int_equal(ntt_log_env_level(), NTT_LOG_NONE);
    setenv("NTT_LOG_LEVEL", "error", 1);
    assert_int_equal(ntt_log_env_level(), NTT_LOG_ERROR);
    setenv("NTT_LOG_LEVEL", "info", 1);
    assert_int_equal(ntt_log_env_level(), NTT_LOG_INFO);
    setenv("NTT_LOG_LEVEL", "debug", 1);
    assert_int_equal(ntt_log_env_level(), NTT_LOG_DEBUG);

    /* Unrecognized values keep the default threshold. */
    setenv("NTT_LOG_LEVEL", "bogus", 1);
    assert_int_equal(ntt_log_env_level(), NTT_LOG_ERROR);

    unsetenv("NTT_LOG_LEVEL");
}

/**
 * Pins the threshold (cascade) semantics: selecting a level keeps that level
 * and every level numerically below it, both via ntt_log_set_level() and the
 * NTT_LOG_LEVEL environment variable. Log output is captured by pointing
 * descriptor 2 at a temp file.
 */
static void emit_and_check(int fd,
                           char *buf,
                           size_t bufsize,
                           int want_error,
                           int want_info,
                           int want_debug)
{
    ftruncate(fd, 0);
    lseek(fd, 0, SEEK_SET);

    ntt__log_write(NTT_LOG_ERROR,
                   __FILE__,
                   __LINE__,
                   __func__,
                   "MSG-%s",
                   "error");
    ntt__log_write(NTT_LOG_INFO,
                   __FILE__,
                   __LINE__,
                   __func__,
                   "MSG-%s",
                   "info");
    ntt__log_write(NTT_LOG_DEBUG,
                   __FILE__,
                   __LINE__,
                   __func__,
                   "MSG-%s",
                   "debug");
    fflush(stderr);

    off_t end = lseek(fd, 0, SEEK_END);
    if (end > (off_t)(bufsize - 1)) {
        end = (off_t)(bufsize - 1);
    }
    lseek(fd, 0, SEEK_SET);
    ssize_t n = read(fd, buf, (size_t)end);
    buf[n] = '\0';

    assert_int_equal(strstr(buf, "MSG-error") != NULL, want_error);
    assert_int_equal(strstr(buf, "MSG-info") != NULL, want_info);
    assert_int_equal(strstr(buf, "MSG-debug") != NULL, want_debug);
}

void torture_ntt_log_cascade(void **state)
{
    (void)state;

    char path[] = "/tmp/ntt_log_cascade_XXXXXX";
    int fd = mkstemp(path);
    assert_int_not_equal(fd, -1);

    int saved_stderr = dup(STDERR_FILENO);
    assert_int_not_equal(saved_stderr, -1);
    assert_int_equal(dup2(fd, STDERR_FILENO), STDERR_FILENO);

    char buf[4096] = {0};

    /* Explicit level set: cascades down from the selected threshold. */
    ntt_log_set_level(NTT_LOG_DEBUG);
    emit_and_check(fd, buf, sizeof(buf), 1, 1, 1);
    ntt_log_set_level(NTT_LOG_INFO);
    emit_and_check(fd, buf, sizeof(buf), 1, 1, 0);
    ntt_log_set_level(NTT_LOG_ERROR);
    emit_and_check(fd, buf, sizeof(buf), 1, 0, 0);

    /* Environment path when nothing has been set explicitly. */
    atomic_store(&g_ntt_log_explicit, 0);
    setenv("NTT_LOG_LEVEL", "debug", 1);
    emit_and_check(fd, buf, sizeof(buf), 1, 1, 1);
    setenv("NTT_LOG_LEVEL", "info", 1);
    emit_and_check(fd, buf, sizeof(buf), 1, 1, 0);
    unsetenv("NTT_LOG_LEVEL");

    assert_int_equal(dup2(saved_stderr, STDERR_FILENO), STDERR_FILENO);
    close(saved_stderr);
    close(fd);
    unlink(path);
}

#endif /* _WIN32 */

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(torture_ntt_log_set_level),
        cmocka_unit_test(torture_ntt_log_set_level_invalid),
#ifndef _WIN32
        cmocka_unit_test(torture_ntt_log_env_level),
        cmocka_unit_test(torture_ntt_log_cascade),
#endif
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
