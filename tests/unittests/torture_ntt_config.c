#include "ntt_config.c"
#include "test_common.h"

/** @brief A freshly allocated config is zeroed with the default type. */
static void torture_ntt_config_new_zeroed(void **state)
{
    (void)state;
    ntt_config *cfg = ntt_config_new();
    assert_non_null(cfg);

    uint64_t q = ntt_config_get_modulus(cfg);
    assert_int_equal(q, 0);

    uint32_t n = ntt_config_get_size(cfg);
    assert_int_equal(n, 0);

    uint64_t omega = ntt_config_get_omega(cfg);
    assert_int_equal(omega, 0);

    uint64_t psi = ntt_config_get_psi(cfg);
    assert_int_equal(psi, 0);

    uint32_t flags = ntt_config_get_flags(cfg);
    assert_int_equal(flags, 0);

    ntt_transform_type type = ntt_config_get_transform_type(cfg);
    assert_int_equal(type, NTT_TRANSFORM_NEGACYCLIC);

    ntt_config_free(cfg);
}

/** @brief Each setter stores a value that the matching getter returns. */
static void torture_ntt_config_set_get_roundtrip(void **state)
{
    (void)state;
    int rc;
    ntt_config *cfg = ntt_config_new();
    assert_non_null(cfg);

    rc = ntt_config_set_modulus(cfg, UINT64_C(12289));
    assert_int_equal(rc, NTT_OK);

    rc = ntt_config_set_size(cfg, 256);
    assert_int_equal(rc, NTT_OK);

    rc = ntt_config_set_omega(cfg, UINT64_C(8340));
    assert_int_equal(rc, NTT_OK);

    rc = ntt_config_set_psi(cfg, UINT64_C(3400));
    assert_int_equal(rc, NTT_OK);

    rc = ntt_config_set_flags(cfg, NTT_CONFIG_REDUCTION_BARRETT);
    assert_int_equal(rc, NTT_OK);

    rc = ntt_config_set_transform_type(cfg, NTT_TRANSFORM_CYCLIC);
    assert_int_equal(rc, NTT_OK);

    uint64_t q = ntt_config_get_modulus(cfg);
    assert_int_equal(q, UINT64_C(12289));

    uint32_t n = ntt_config_get_size(cfg);
    assert_int_equal(n, 256);

    uint64_t omega = ntt_config_get_omega(cfg);
    assert_int_equal(omega, UINT64_C(8340));

    uint64_t psi = ntt_config_get_psi(cfg);
    assert_int_equal(psi, UINT64_C(3400));

    uint32_t flags = ntt_config_get_flags(cfg);
    assert_int_equal(flags, NTT_CONFIG_REDUCTION_BARRETT);

    ntt_transform_type type = ntt_config_get_transform_type(cfg);
    assert_int_equal(type, NTT_TRANSFORM_CYCLIC);

    ntt_config_free(cfg);
}

/** @brief NULL arguments are rejected and yield documented defaults. */
static void torture_ntt_config_null_safe(void **state)
{
    (void)state;
    int rc;

    rc = ntt_config_set_modulus(NULL, 1);
    assert_int_equal(rc, NTT_ERROR);

    rc = ntt_config_set_size(NULL, 1);
    assert_int_equal(rc, NTT_ERROR);

    rc = ntt_config_set_omega(NULL, 1);
    assert_int_equal(rc, NTT_ERROR);

    rc = ntt_config_set_psi(NULL, 1);
    assert_int_equal(rc, NTT_ERROR);

    rc = ntt_config_set_flags(NULL, 1);
    assert_int_equal(rc, NTT_ERROR);

    rc = ntt_config_set_transform_type(NULL, NTT_TRANSFORM_CYCLIC);
    assert_int_equal(rc, NTT_ERROR);

    uint64_t q = ntt_config_get_modulus(NULL);
    assert_int_equal(q, 0);

    uint32_t n = ntt_config_get_size(NULL);
    assert_int_equal(n, 0);

    uint64_t omega = ntt_config_get_omega(NULL);
    assert_int_equal(omega, 0);

    uint64_t psi = ntt_config_get_psi(NULL);
    assert_int_equal(psi, 0);

    uint32_t flags = ntt_config_get_flags(NULL);
    assert_int_equal(flags, 0);

    ntt_transform_type type = ntt_config_get_transform_type(NULL);
    assert_int_equal(type, NTT_TRANSFORM_NEGACYCLIC);
}

/** @brief Releasing a NULL config is a no-op. */
static void torture_ntt_config_free_null(void **state)
{
    (void)state;
    ntt_config_free(NULL);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(torture_ntt_config_new_zeroed),
        cmocka_unit_test(torture_ntt_config_set_get_roundtrip),
        cmocka_unit_test(torture_ntt_config_null_safe),
        cmocka_unit_test(torture_ntt_config_free_null),
    };

    ntt_test_set_log_level();
    return cmocka_run_group_tests(tests, NULL, NULL);
}
