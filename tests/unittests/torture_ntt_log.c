#include "ntt_log.c"
#include "test_common.h"

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

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(torture_ntt_log_set_level),
        cmocka_unit_test(torture_ntt_log_set_level_invalid),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
