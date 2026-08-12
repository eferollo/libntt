#define _POSIX_C_SOURCE 200809L

#include "ntt_adapter.c"
#include "test_common.h"
#include <unistd.h>

#define TEST_NAME_OVERSIZE 200
#define TEST_DIR_OVERSIZE  5000

/*
 * Mock descriptors returned by the stubbed built-in getters below. They are
 * well-formed (full ABI version and struct size) but are never backed by the
 * real adapter implementations.
 *
 * (a) mock_full advertises every capability and reduction flag;
 * (b) mock_minimal advertises only runtime modulus support.
 */
static ntt_adapter mock_full = {
    .abi_version = NTT_ADAPTER_ABI_VERSION,
    .struct_size = sizeof(ntt_adapter),
    .name = "mock_full",
    .capabilities =
        NTT_CAP_RUNTIME_MODULUS | NTT_CAP_BARRETT | NTT_CAP_MONTGOMERY,
    .supported_flags =
        NTT_CONFIG_REDUCTION_BARRETT | NTT_CONFIG_REDUCTION_MONTGOMERY,
};

static ntt_adapter mock_minimal = {
    .abi_version = NTT_ADAPTER_ABI_VERSION,
    .struct_size = sizeof(ntt_adapter),
    .name = "mock_minimal",
    .capabilities = NTT_CAP_RUNTIME_MODULUS,
    .supported_flags = 0,
};

/*
 * Stubs replacing the library's built-in adapter getters, so every reference
 * from the compiled-in ntt_adapter.c and the linked registry resolves to the
 * mocks above instead of the real adapter implementations.
 */
const ntt_adapter *ntt_adapter_scalar(void)
{
    return &mock_full;
}

const ntt_adapter *ntt_adapter_scalar_toy(void)
{
    return &mock_minimal;
}

/** @brief Clears the configuration environment and the cached default. */
static void reset_env(void)
{
    unsetenv("NTT_CONFIG_FILE");
    unsetenv("NTT_ADAPTER_MODULE_DIR");
    unsetenv("HOME");
    ntt__adapter_reset_default();
}

/** @brief Builds a minimal descriptor with controllable ABI fields. */
static ntt_adapter
make_fake(uint32_t abi_version, uint32_t struct_size, uint32_t supported_flags)
{
    ntt_adapter adapter;
    memset(&adapter, 0, sizeof(adapter));
    adapter.abi_version = abi_version;
    adapter.struct_size = struct_size;
    adapter.supported_flags = supported_flags;
    return adapter;
}

/** @brief ntt_adapter_get_abi_version: NULL, mocks and a fake. */
static void torture_ntt_adapter_get_abi_version(void **state)
{
    (void)state;
    uint32_t abi;

    abi = ntt_adapter_get_abi_version(NULL);
    assert_int_equal(abi, 0);

    assert_non_null(&mock_full);
    assert_non_null(&mock_minimal);

    abi = ntt_adapter_get_abi_version(&mock_full);
    assert_int_equal(abi, NTT_ADAPTER_ABI_VERSION);
    abi = ntt_adapter_get_abi_version(&mock_minimal);
    assert_int_equal(abi, NTT_ADAPTER_ABI_VERSION);

    ntt_adapter fake = make_fake(0xFFAA, 0, 0);
    abi = ntt_adapter_get_abi_version(&fake);
    assert_int_equal(abi, 0xFFAA);
}

/** @brief ntt_adapter_get_struct_size: NULL, mocks and a fake. */
static void torture_ntt_adapter_get_struct_size(void **state)
{
    (void)state;
    uint32_t size;

    size = ntt_adapter_get_struct_size(NULL);
    assert_int_equal(size, 0);

    size = ntt_adapter_get_struct_size(&mock_full);
    assert_int_equal(size, (uint32_t)sizeof(ntt_adapter));
    size = ntt_adapter_get_struct_size(&mock_minimal);
    assert_int_equal(size, (uint32_t)sizeof(ntt_adapter));

    ntt_adapter fake = make_fake(0, 42, 0);
    size = ntt_adapter_get_struct_size(&fake);
    assert_int_equal(size, 42);
}

/** @brief ntt_adapter_get_name: NULL, mocks and a fake. */
static void torture_ntt_adapter_get_name(void **state)
{
    (void)state;
    const char *name = NULL;

    name = ntt_adapter_get_name(NULL);
    assert_null(name);

    name = ntt_adapter_get_name(&mock_full);
    assert_string_equal(name, "mock_full");
    name = ntt_adapter_get_name(&mock_minimal);
    assert_string_equal(name, "mock_minimal");

    ntt_adapter fake = make_fake(0, sizeof(ntt_adapter), 0);
    fake.name = "custom";
    name = ntt_adapter_get_name(&fake);
    assert_string_equal(name, "custom");
    fake.name = NULL;
    name = ntt_adapter_get_name(&fake);
    assert_null(name);
}

/** @brief ntt_adapter_get_capabilities: NULL, mocks and a fake. */
static void torture_ntt_adapter_get_capabilities(void **state)
{
    (void)state;
    uint32_t caps;

    caps = ntt_adapter_get_capabilities(NULL);
    assert_int_equal(caps, 0);

    const uint32_t expected_full =
        NTT_CAP_RUNTIME_MODULUS | NTT_CAP_BARRETT | NTT_CAP_MONTGOMERY;
    caps = ntt_adapter_get_capabilities(&mock_full);
    assert_int_equal(caps, expected_full);
    caps = ntt_adapter_get_capabilities(&mock_minimal);
    assert_int_equal(caps, NTT_CAP_RUNTIME_MODULUS);

    ntt_adapter fake = make_fake(0, sizeof(ntt_adapter), 0);
    fake.capabilities = NTT_CAP_SIMD;
    caps = ntt_adapter_get_capabilities(&fake);
    assert_int_equal(caps, NTT_CAP_SIMD);
}

/** @brief ntt_adapter_get_supported_flags: NULL, mocks and a fake. */
static void torture_ntt_adapter_get_supported_flags(void **state)
{
    (void)state;
    uint32_t flags;

    flags = ntt_adapter_get_supported_flags(NULL);
    assert_int_equal(flags, 0);

    const uint32_t expected_full =
        NTT_CONFIG_REDUCTION_BARRETT | NTT_CONFIG_REDUCTION_MONTGOMERY;
    flags = ntt_adapter_get_supported_flags(&mock_full);
    assert_int_equal(flags, expected_full);
    flags = ntt_adapter_get_supported_flags(&mock_minimal);
    assert_int_equal(flags, 0);

    ntt_adapter fake = make_fake(0, sizeof(ntt_adapter), 0);
    fake.supported_flags = NTT_CONFIG_REDUCTION_BARRETT;
    flags = ntt_adapter_get_supported_flags(&fake);
    assert_int_equal(flags, NTT_CONFIG_REDUCTION_BARRETT);
}

/** @brief ntt_adapter_supports_flags: subsets, unknown flags and NULL. */
static void torture_ntt_adapter_supports_flags(void **state)
{
    (void)state;
    bool supported;

    supported = ntt_adapter_supports_flags(NULL, 0);
    assert_false(supported);
    supported = ntt_adapter_supports_flags(NULL, NTT_CONFIG_REDUCTION_BARRETT);
    assert_false(supported);

    supported = ntt_adapter_supports_flags(&mock_full, 0);
    assert_true(supported);
    supported =
        ntt_adapter_supports_flags(&mock_full, NTT_CONFIG_REDUCTION_BARRETT);
    assert_true(supported);
    supported = ntt_adapter_supports_flags(&mock_full,
                                           NTT_CONFIG_REDUCTION_BARRETT |
                                               NTT_CONFIG_REDUCTION_MONTGOMERY);
    assert_true(supported);
    supported =
        ntt_adapter_supports_flags(&mock_full,
                                   NTT_CONFIG_REDUCTION_BARRETT | NTT_CAP_SIMD);
    assert_false(supported);
    supported = ntt_adapter_supports_flags(&mock_full, 1u << 16);
    assert_false(supported);

    supported = ntt_adapter_supports_flags(&mock_minimal, 0);
    assert_true(supported);
    supported =
        ntt_adapter_supports_flags(&mock_minimal, NTT_CONFIG_REDUCTION_BARRETT);
    assert_false(supported);
}

/** @brief ntt__adapter_has_field: boundaries, NULL and mock descriptors. */
static void torture_ntt_adapter_has_field(void **state)
{
    (void)state;
    bool present;

    present = ntt__adapter_has_field(NULL, 0, 0);
    assert_false(present);

    ntt_adapter fake = make_fake(0, 8, 0);
    present = ntt__adapter_has_field(&fake, 0, 0);
    assert_true(present);
    present = ntt__adapter_has_field(&fake, 4, 4);
    assert_true(present);
    present = ntt__adapter_has_field(&fake, 8, 0);
    assert_true(present);
    present = ntt__adapter_has_field(&fake, 4, 5);
    assert_false(present);
    present = ntt__adapter_has_field(&fake, 9, 0);
    assert_false(present);
    present = ntt__adapter_has_field(&fake, 0, 9);
    assert_false(present);

    present = ntt__adapter_has_field(&mock_full,
                                     offsetof(ntt_adapter, name),
                                     sizeof(char *));
    assert_true(present);
    present = ntt__adapter_has_field(&mock_full,
                                     offsetof(ntt_adapter, negacyclic_mul),
                                     sizeof(ntt_adapter_negacyclic_mul_fn));
    assert_true(present);
    present = ntt__adapter_has_field(&mock_full, sizeof(ntt_adapter), 1);
    assert_false(present);
}

/** @brief ntt__adapter_is_compatible: version and size gates, NULL. */
static void torture_ntt_adapter_is_compatible(void **state)
{
    (void)state;
    bool compatible;

    compatible = ntt__adapter_is_compatible(NULL);
    assert_false(compatible);

    const size_t required_size = offsetof(ntt_adapter, negacyclic_mul) +
                                 sizeof(ntt_adapter_negacyclic_mul_fn);

    compatible = ntt__adapter_is_compatible(&mock_full);
    assert_true(compatible);
    compatible = ntt__adapter_is_compatible(&mock_minimal);
    assert_true(compatible);

    ntt_adapter fake =
        make_fake(NTT_ADAPTER_ABI_VERSION, (uint32_t)required_size, 0);
    compatible = ntt__adapter_is_compatible(&fake);
    assert_true(compatible);

    fake.struct_size = (uint32_t)(required_size - 1);
    compatible = ntt__adapter_is_compatible(&fake);
    assert_false(compatible);

    fake.struct_size = sizeof(ntt_adapter);
    fake.abi_version = NTT_ADAPTER_ABI_VERSION + 1;
    compatible = ntt__adapter_is_compatible(&fake);
    assert_false(compatible);

    /* An older ABI version with a full structure stays compatible. */
    fake.abi_version = 0;
    compatible = ntt__adapter_is_compatible(&fake);
    assert_true(compatible);
}

/** @brief ntt__adapter_reset_default clears override, cache and name. */
static void torture_ntt_adapter_reset_default(void **state)
{
    (void)state;
    int rc;
    const ntt_adapter *adapter = NULL;
    const char *name = NULL;
    reset_env();

    rc = ntt_adapter_set_default("scalar_toy", "/opt/mod");
    assert_int_equal(rc, NTT_OK);
    assert_true(ntt__default_explicit);
    assert_string_equal(ntt__default_name, "scalar_toy");
    assert_string_equal(ntt__default_module_dir, "/opt/mod");
    assert_null(ntt__default_cache);

    adapter = ntt_adapter_get_default();
    assert_non_null(adapter);
    assert_non_null(ntt__default_cache);

    ntt__adapter_reset_default();
    assert_false(ntt__default_explicit);
    assert_string_equal(ntt__default_name, "");
    assert_string_equal(ntt__default_module_dir, "");
    assert_null(ntt__default_cache);
    adapter = ntt_adapter_get_default();
    name = ntt_adapter_get_name(adapter);
    assert_string_equal(name, "mock_full");

    /* Resetting again is a safe no-op. */
    ntt__adapter_reset_default();
    assert_false(ntt__default_explicit);
}

/** @brief ntt_adapter_get: every selector, the fallback and NULL-safety. */
static void torture_ntt_adapter_get(void **state)
{
    (void)state;
    const ntt_adapter *adapter = NULL, *out_of_range = NULL;
    const char *name = NULL;
    reset_env();

    adapter = ntt_adapter_get(NTT_ADAPTER_SCALAR);
    name = ntt_adapter_get_name(adapter);
    assert_string_equal(name, "mock_full");

    adapter = ntt_adapter_get(NTT_ADAPTER_SCALAR_TOY);
    name = ntt_adapter_get_name(adapter);
    assert_string_equal(name, "mock_minimal");

    adapter = ntt_adapter_get(NTT_ADAPTER_DEFAULT);
    name = ntt_adapter_get_name(adapter);
    assert_string_equal(name, "mock_full");

    out_of_range = ntt_adapter_get((ntt_adapter_selector)999);
    assert_non_null(out_of_range);
    name = ntt_adapter_get_name(out_of_range);
    assert_string_equal(name, "mock_full");
}

/** @brief ntt_adapter_set_default: validation, truncation and cache reset. */
static void torture_ntt_adapter_set_default(void **state)
{
    (void)state;
    int rc;
    const ntt_adapter *adapter = NULL;
    reset_env();

    rc = ntt_adapter_set_default(NULL, NULL);
    assert_int_equal(rc, NTT_ERROR);
    rc = ntt_adapter_set_default("", NULL);
    assert_int_equal(rc, NTT_ERROR);
    assert_false(ntt__default_explicit);

    rc = ntt_adapter_set_default("scalar_toy", NULL);
    assert_int_equal(rc, NTT_OK);
    assert_true(ntt__default_explicit);
    assert_string_equal(ntt__default_name, "scalar_toy");
    assert_string_equal(ntt__default_module_dir, "");

    rc = ntt_adapter_set_default("external", "/mod/dir");
    assert_int_equal(rc, NTT_OK);
    assert_string_equal(ntt__default_name, "external");
    assert_string_equal(ntt__default_module_dir, "/mod/dir");

    /* A NULL module_dir clears a previously stored directory. */
    rc = ntt_adapter_set_default("scalar", NULL);
    assert_int_equal(rc, NTT_OK);
    assert_string_equal(ntt__default_module_dir, "");

    char long_name[TEST_NAME_OVERSIZE] = {0};
    memset(long_name, 'a', sizeof(long_name) - 1);
    long_name[sizeof(long_name) - 1] = '\0';
    rc = ntt_adapter_set_default(long_name, NULL);
    assert_int_equal(rc, NTT_ERROR);
    assert_true(ntt__default_explicit);

    char long_dir[TEST_DIR_OVERSIZE] = {0};
    memset(long_dir, 'b', sizeof(long_dir) - 1);
    long_dir[sizeof(long_dir) - 1] = '\0';
    rc = ntt_adapter_set_default("scalar", long_dir);
    assert_int_equal(rc, NTT_ERROR);

    /* Re-setting clears any previously cached resolution. */
    adapter = ntt_adapter_get_default();
    assert_non_null(adapter);
    assert_non_null(ntt__default_cache);
    rc = ntt_adapter_set_default("scalar_toy", NULL);
    assert_int_equal(rc, NTT_OK);
    assert_null(ntt__default_cache);
}

/** @brief ntt_adapter_get_default: fallback, override, cache and config. */
static void torture_ntt_adapter_get_default(void **state)
{
    (void)state;
    char path[64] = {0};
    FILE *fp = NULL;
    int rc, expected;
    const ntt_adapter *adapter = NULL, *again = NULL;
    const char *name = NULL;

    reset_env();
    adapter = ntt_adapter_get_default();
    name = ntt_adapter_get_name(adapter);
    assert_string_equal(name, "mock_full");

    /* The resolved descriptor is cached until a reset. */
    adapter = ntt_adapter_get_default();
    again = ntt_adapter_get_default();
    assert_ptr_equal(adapter, again);

    rc = ntt_adapter_set_default("scalar_toy", NULL);
    assert_int_equal(rc, NTT_OK);
    adapter = ntt_adapter_get_default();
    name = ntt_adapter_get_name(adapter);
    assert_string_equal(name, "mock_minimal");

    /* Non-explicit resolution honors NTT_CONFIG_FILE. */
    reset_env();
    rc = snprintf(path,
                  sizeof(path),
                  "/tmp/ntt_adapter_cfg_%ld.conf",
                  (long)getpid());
    assert_true(rc > 0 && rc < (int)sizeof(path));
    fp = fopen(path, "w");
    assert_non_null(fp);
    rc = fprintf(fp, "adapter = scalar_toy\n");
    expected = (int)strlen("adapter = scalar_toy\n");
    assert_int_equal(rc, expected);
    rc = fclose(fp);
    assert_int_equal(rc, 0);
    rc = setenv("NTT_CONFIG_FILE", path, 1);
    assert_int_equal(rc, 0);
    adapter = ntt_adapter_get_default();
    name = ntt_adapter_get_name(adapter);
    assert_string_equal(name, "mock_minimal");
    rc = unsetenv("NTT_CONFIG_FILE");
    assert_int_equal(rc, 0);
    rc = remove(path);
    assert_int_equal(rc, 0);
}

/** @brief ntt__resolve_default: mocks, unknown names and fallback. */
static void torture_ntt_adapter_resolve_default(void **state)
{
    (void)state;
    const ntt_adapter *adapter = NULL;
    const char *name = NULL;
    reset_env();

    adapter = ntt__resolve_default(NULL, NULL);
    name = ntt_adapter_get_name(adapter);
    assert_string_equal(name, "mock_full");

    adapter = ntt__resolve_default("", NULL);
    name = ntt_adapter_get_name(adapter);
    assert_string_equal(name, "mock_full");

    adapter = ntt__resolve_default("scalar", NULL);
    name = ntt_adapter_get_name(adapter);
    assert_string_equal(name, "mock_full");

    adapter = ntt__resolve_default("scalar_toy", NULL);
    name = ntt_adapter_get_name(adapter);
    assert_string_equal(name, "mock_minimal");

    adapter = ntt__resolve_default("no_such", NULL);
    name = ntt_adapter_get_name(adapter);
    assert_string_equal(name, "mock_full");

    adapter = ntt__resolve_default("no_such", "");
    name = ntt_adapter_get_name(adapter);
    assert_string_equal(name, "mock_full");

    adapter = ntt__resolve_default("no_such", "/nonexistent");
    name = ntt_adapter_get_name(adapter);
    assert_string_equal(name, "mock_full");

    adapter = ntt__resolve_default(NULL, NULL);
    assert_non_null(adapter);
}

/** @brief ntt_adapter_load: NULL name, registry selection and module load. */
static void torture_ntt_adapter_load(void **state)
{
    (void)state;
    const ntt_adapter *adapter = NULL;
    const char *name = NULL;
    reset_env();

    adapter = ntt_adapter_load(NULL, NULL);
    assert_null(adapter);
    adapter = ntt_adapter_load(NULL, "/tmp/ntt/adapters");
    assert_null(adapter);

    /* module_dir NULL: built-in registry selection. */
    adapter = ntt_adapter_load("scalar", NULL);
    name = ntt_adapter_get_name(adapter);
    assert_string_equal(name, "mock_full");
    adapter = ntt_adapter_load("scalar_toy", NULL);
    name = ntt_adapter_get_name(adapter);
    assert_string_equal(name, "mock_minimal");
    adapter = ntt_adapter_load("no_such_adapter", NULL);
    assert_null(adapter);

    /* module_dir set: external module load, which fails for a missing
     * directory (the real dlopen never succeeds here). */
    adapter = ntt_adapter_load("ext", "/nonexistent/ntt/adapters");
    assert_null(adapter);
}

/** @brief ntt_adapter_unload_all: resets the default adapter state. */
static void torture_ntt_adapter_unload_all(void **state)
{
    (void)state;
    int rc;
    const ntt_adapter *adapter = NULL;
    const char *name = NULL;
    reset_env();

    rc = ntt_adapter_set_default("scalar_toy", NULL);
    assert_int_equal(rc, NTT_OK);
    adapter = ntt_adapter_get_default();
    name = ntt_adapter_get_name(adapter);
    assert_string_equal(name, "mock_minimal");
    assert_true(ntt__default_explicit);
    assert_non_null(ntt__default_cache);

    ntt_adapter_unload_all();

    /* The override and cached resolution are cleared. */
    assert_false(ntt__default_explicit);
    assert_null(ntt__default_cache);
    adapter = ntt_adapter_get_default();
    name = ntt_adapter_get_name(adapter);
    assert_string_equal(name, "mock_full");

    /* Calling it again is a safe no-op. */
    ntt_adapter_unload_all();
    assert_null(ntt__default_cache);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(torture_ntt_adapter_get_abi_version),
        cmocka_unit_test(torture_ntt_adapter_get_struct_size),
        cmocka_unit_test(torture_ntt_adapter_get_name),
        cmocka_unit_test(torture_ntt_adapter_get_capabilities),
        cmocka_unit_test(torture_ntt_adapter_get_supported_flags),
        cmocka_unit_test(torture_ntt_adapter_supports_flags),
        cmocka_unit_test(torture_ntt_adapter_has_field),
        cmocka_unit_test(torture_ntt_adapter_is_compatible),
        cmocka_unit_test(torture_ntt_adapter_reset_default),
        cmocka_unit_test(torture_ntt_adapter_get),
        cmocka_unit_test(torture_ntt_adapter_set_default),
        cmocka_unit_test(torture_ntt_adapter_get_default),
        cmocka_unit_test(torture_ntt_adapter_resolve_default),
        cmocka_unit_test(torture_ntt_adapter_load),
        cmocka_unit_test(torture_ntt_adapter_unload_all),
    };

    ntt_test_set_log_level();
    return cmocka_run_group_tests(tests, NULL, NULL);
}
