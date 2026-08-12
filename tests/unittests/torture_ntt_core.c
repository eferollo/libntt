#include "ntt_core.c"
#include "test_common.h"

/*
 * A crafted core exposing only one id, used to prove ntt_core_find() returns
 * NULL for ids that are missing from a stream.
 */
static const ntt_dispatch partial_ops[] = {
    {
        NTT_FUNC_CONFIG_GET_SIZE,
        (void (*)(void))ntt_config_get_size,
    },
    {
        NTT_FUNC_NONE,
        NULL,
    },
};

static const ntt_core_api partial_api = {
    .version = NTT_CORE_API_VERSION,
    .struct_size = sizeof(ntt_core_api),
    .ops = partial_ops,
};

/* A crafted core one version behind the current one, for compatibility. */
static const ntt_core_api older_api = {
    .version = NTT_CORE_API_VERSION - 1,
    .struct_size = sizeof(ntt_core_api),
    .ops = NULL,
};

/**
 * @brief Stores @p value with a config setter, then reads it back through the
 *        matching typed core accessor.
 *
 * Covers the setter, the dispatch lookup and the typed accessor in one shot.
 */
#define NTT_TEST_CORE_SET_GET(cfg, api, setter, getter, value) \
    do {                                                       \
        int set_rc = setter(cfg, value);                       \
        assert_int_equal(set_rc, NTT_OK);                      \
        uint64_t got = getter(api, cfg);                       \
        assert_int_equal(got, (uint64_t)(value));              \
    } while (0)

/** @brief ntt__core_api(): a well-formed, complete instance is returned. */
static void torture_ntt_core_api(void **state)
{
    (void)state;
    const ntt_core_api *api = ntt__core_api();
    uint32_t version;
    uint32_t size;

    assert_non_null(api);
    assert_non_null(api->ops);

    version = api->version;
    assert_int_equal(version, NTT_CORE_API_VERSION);

    size = api->struct_size;
    assert_int_equal(size, sizeof(ntt_core_api));
}

/** @brief The static dispatch stream is complete, terminated and unique. */
static void torture_ntt_core_dispatch_table(void **state)
{
    (void)state;
    const ntt_core_api *api = ntt__core_api();
    const ntt_dispatch *d;
    bool seen[NTT_FUNC_UTIL_IS_PRIME + 1] = {false};
    int count = 0;

    assert_non_null(api);
    assert_non_null(api->ops);

    for (d = api->ops; d != NULL && d->id != NTT_FUNC_NONE; ++d) {
        assert_true(d->id >= NTT_FUNC_CONFIG_GET_MODULUS);
        assert_true(d->id <= NTT_FUNC_UTIL_IS_PRIME);
        assert_non_null(d->fn);
        assert_false(seen[d->id]);
        seen[d->id] = true;
        count++;
    }

    assert_int_equal(count, NTT_FUNC_UTIL_IS_PRIME);

    for (ntt_dispatch_id id = NTT_FUNC_CONFIG_GET_MODULUS;
         id <= NTT_FUNC_UTIL_IS_PRIME;
         ++id) {
        assert_true(seen[id]);
    }
}

/** @brief ntt_core_find(): known ids, terminator, missing ids and NULL. */
static void torture_ntt_core_find(void **state)
{
    (void)state;
    const ntt_core_api *api = ntt__core_api();
    const ntt_dispatch *d;

    assert_non_null(api);

    d = ntt_core_find(NULL, NTT_FUNC_CONFIG_GET_MODULUS);
    assert_null(d);

    d = ntt_core_find(api, NTT_FUNC_NONE);
    assert_null(d);

    d = ntt_core_find(api, (ntt_dispatch_id)100);
    assert_null(d);

    for (ntt_dispatch_id id = NTT_FUNC_CONFIG_GET_MODULUS;
         id <= NTT_FUNC_UTIL_IS_PRIME;
         ++id) {
        d = ntt_core_find(api, id);
        assert_non_null(d);
        assert_int_equal(d->id, id);
        assert_non_null(d->fn);
    }

    d = ntt_core_find(&partial_api, NTT_FUNC_CONFIG_GET_SIZE);
    assert_non_null(d);
    assert_int_equal(d->id, NTT_FUNC_CONFIG_GET_SIZE);

    d = ntt_core_find(&partial_api, NTT_FUNC_CONFIG_GET_MODULUS);
    assert_null(d);
}

/** @brief ntt_core_is_compatible(): version gates and NULL. */
static void torture_ntt_core_is_compatible(void **state)
{
    (void)state;
    const ntt_core_api *api = ntt__core_api();
    bool compatible;

    compatible = ntt_core_is_compatible(NULL, NTT_CORE_API_VERSION);
    assert_false(compatible);

    compatible = ntt_core_is_compatible(api, 0);
    assert_true(compatible);

    compatible = ntt_core_is_compatible(api, NTT_CORE_API_VERSION);
    assert_true(compatible);

    compatible = ntt_core_is_compatible(api, NTT_CORE_API_VERSION + 1);
    assert_false(compatible);

    compatible = ntt_core_is_compatible(&older_api, NTT_CORE_API_VERSION - 1);
    assert_true(compatible);

    compatible = ntt_core_is_compatible(&older_api, NTT_CORE_API_VERSION);
    assert_false(compatible);
}

/** @brief Typed accessors reach the real config through the dispatch table. */
static void torture_ntt_core_typed_accessors(void **state)
{
    /* basically like testing ntt_config API */
    (void)state;
    const ntt_core_api *api = ntt__core_api();
    ntt_config *cfg = ntt_config_new();
    bool prime;

    assert_non_null(api);
    assert_non_null(cfg);

    NTT_TEST_CORE_SET_GET(cfg,
                          api,
                          ntt_config_set_modulus,
                          ntt_core_get_modulus,
                          UINT64_C(12289));
    NTT_TEST_CORE_SET_GET(cfg,
                          api,
                          ntt_config_set_size,
                          ntt_core_get_size,
                          256);
    NTT_TEST_CORE_SET_GET(cfg,
                          api,
                          ntt_config_set_transform_type,
                          ntt_core_get_transform_type,
                          NTT_TRANSFORM_CYCLIC);
    NTT_TEST_CORE_SET_GET(cfg,
                          api,
                          ntt_config_set_flags,
                          ntt_core_get_flags,
                          NTT_CONFIG_REDUCTION_BARRETT);
    NTT_TEST_CORE_SET_GET(cfg,
                          api,
                          ntt_config_set_omega,
                          ntt_core_get_omega,
                          UINT64_C(8340));
    NTT_TEST_CORE_SET_GET(cfg,
                          api,
                          ntt_config_set_psi,
                          ntt_core_get_psi,
                          UINT64_C(3400));

    prime = ntt_core_is_prime(api, UINT64_C(12289));
    assert_true(prime);
    prime = ntt_core_is_prime(api, UINT64_C(12288));
    assert_false(prime);

    ntt_config_free(cfg);
}

/** @brief ntt__dlopen/ntt__dlsym/ntt__dlclose: real and failing loads. */
static void torture_ntt_dl_open_close(void **state)
{
    (void)state;
    void *handle;
    void *sym;

    handle = ntt__dlopen(NULL);
    assert_non_null(handle);

    sym = ntt__dlsym(handle, "printf");
    assert_non_null(sym);

    sym = ntt__dlsym(handle, "no_such_symbol_ntt");
    assert_null(sym);

    handle = ntt__dlopen("/nonexistent/libntt_does_not_exist.so");
    assert_null(handle);

    handle = ntt__dlopen(NULL);
    assert_non_null(handle);
    ntt__dlclose(handle);
}

/** @brief ntt__dl_extension(): the platform shared-library suffix. */
static void torture_ntt_dl_extension(void **state)
{
    (void)state;
    const char *ext = ntt__dl_extension();

    assert_non_null(ext);
#if defined(_WIN32)
    assert_string_equal(ext, ".dll");
#else
    assert_string_equal(ext, ".so");
#endif
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(torture_ntt_core_api),
        cmocka_unit_test(torture_ntt_core_dispatch_table),
        cmocka_unit_test(torture_ntt_core_find),
        cmocka_unit_test(torture_ntt_core_is_compatible),
        cmocka_unit_test(torture_ntt_core_typed_accessors),
        cmocka_unit_test(torture_ntt_dl_open_close),
        cmocka_unit_test(torture_ntt_dl_extension),
    };

    ntt_test_set_log_level();
    return cmocka_run_group_tests(tests, NULL, NULL);
}
