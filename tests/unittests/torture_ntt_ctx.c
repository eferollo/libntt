#include "ntt_ctx.c"
#include "test_common.h"

#include <setjmp.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <cmocka.h>

/* q = 2^12 * 3 + 1, a valid transform domain for the mock adapter. */
#define TEST_Q UINT64_C(12289)
#define TEST_N 256

/* Tracks the mock adapter's callback invocations and failure toggles. */
typedef struct {
    bool fail_validate; /* make validate_modulus() reject the modulus */
    bool fail_setup;    /* make setup() return NULL */
    size_t validate_calls;
    size_t setup_calls;
    size_t teardown_calls;
    void *setup_state;
} mock_counters;

static mock_counters mock;

/* Per-test state carried through the cmocka state variable. */
typedef struct {
    ntt_adapter *adapter;
    ntt_config *cfg;
} ntt_state;

static bool mock_adapter_validate_modulus(const ntt_config *config,
                                          const ntt_core_api *api)
{
    (void)config;
    (void)api;
    mock.validate_calls++;
    return !mock.fail_validate;
}

static void *mock_adapter_setup(const ntt_config *config,
                                const ntt_core_api *api)
{
    (void)config;
    (void)api;
    mock.setup_calls++;
    if (mock.fail_setup) {
        return NULL;
    }
    mock.setup_state = calloc(1, sizeof(char));
    return mock.setup_state;
}

static void mock_adapter_teardown(void *state)
{
    mock.teardown_calls++;
    mock.setup_state = NULL;
    free(state);
}

/** @brief Builds a well-formed mock adapter descriptor. */
static ntt_adapter mock_adapter_descriptor(void)
{
    ntt_adapter a;

    memset(&a, 0, sizeof(a));
    a.abi_version = NTT_ADAPTER_ABI_VERSION;
    a.struct_size = sizeof(a);
    a.name = "mock";
    a.capabilities = NTT_CAP_RUNTIME_MODULUS;
    a.supported_flags = 0;
    a.validate_modulus = mock_adapter_validate_modulus;
    a.setup = mock_adapter_setup;
    a.teardown = mock_adapter_teardown;
    return a;
}

/** @brief Allocates a configuration with the given modulus and size. */
static ntt_config *cfg_new(uint64_t q, uint32_t n)
{
    int rc;
    ntt_config *cfg = ntt_config_new();
    assert_non_null(cfg);

    rc = ntt_config_set_modulus(cfg, q);
    assert_int_equal(rc, NTT_OK);

    rc = ntt_config_set_size(cfg, n);
    assert_int_equal(rc, NTT_OK);
    return cfg;
}

/** @brief Resets the mock counters and installs the per-test state. */
static int setup_ntt_state(void **state)
{
    ntt_state *ns = calloc(1, sizeof(*ns));
    assert_non_null(ns);

    memset(&mock, 0, sizeof(mock));

    ns->adapter = calloc(1, sizeof(*ns->adapter));
    assert_non_null(ns->adapter);
    *ns->adapter = mock_adapter_descriptor();

    ns->cfg = cfg_new(TEST_Q, TEST_N);
    assert_non_null(ns->cfg);

    *state = ns;
    return 0;
}

/** @brief Releases the test state installed by setup_ntt_state(). */
static int teardown_ntt_state(void **state)
{
    ntt_state *ns = *state;

    ntt_config_free(ns->cfg);
    free(ns->adapter);
    free(ns);
    return 0;
}

/** @brief ntt_create rejects a NULL adapter. */
static void torture_ntt_create_null_adapter(void **state)
{
    ntt_state *ns = *state;

    ntt_ctx *ctx = ntt_create(NULL, ns->cfg);
    assert_null(ctx);
}

/** @brief ntt_create rejects a NULL configuration. */
static void torture_ntt_create_null_config(void **state)
{
    ntt_state *ns = *state;

    ntt_ctx *ctx = ntt_create(ns->adapter, NULL);
    assert_null(ctx);
}

/** @brief ntt_create rejects NULL arguments. */
static void torture_ntt_create_null_arguments(void **state)
{
    (void)state;
    ntt_ctx *ctx = ntt_create(NULL, NULL);
    assert_null(ctx);
}

/** @brief An adapter with a too-new ABI version is rejected. */
static void torture_ntt_create_rejects_newer_abi(void **state)
{
    ntt_state *ns = *state;

    ns->adapter->abi_version = NTT_ADAPTER_ABI_VERSION + 1u;
    ntt_ctx *ctx = ntt_create(ns->adapter, ns->cfg);
    assert_null(ctx);
}

/** @brief An adapter smaller than the current ABI contract is rejected. */
static void torture_ntt_create_rejects_small_struct(void **state)
{
    ntt_state *ns = *state;

    ns->adapter->struct_size = offsetof(ntt_adapter, negacyclic_mul);
    ntt_ctx *ctx = ntt_create(ns->adapter, ns->cfg);
    assert_null(ctx);
}

/** @brief An older ABI version with a complete descriptor is accepted. */
static void torture_ntt_create_accepts_older_abi(void **state)
{
    ntt_state *ns = *state;

    ns->adapter->abi_version = NTT_ADAPTER_ABI_VERSION - 1u;
    ntt_ctx *ctx = ntt_create(ns->adapter, ns->cfg);
    assert_non_null(ctx);

    ntt_destroy(ctx);
}

/** @brief A NULL validate_modulus, setup or teardown callback is rejected. */
static void torture_ntt_create_rejects_missing_callbacks(void **state)
{
    ntt_state *ns = *state;
    ntt_ctx *ctx = NULL;

    *ns->adapter = mock_adapter_descriptor();
    ns->adapter->validate_modulus = NULL;
    ctx = ntt_create(ns->adapter, ns->cfg);
    assert_null(ctx);

    *ns->adapter = mock_adapter_descriptor();
    ns->adapter->setup = NULL;
    ctx = ntt_create(ns->adapter, ns->cfg);
    assert_null(ctx);

    *ns->adapter = mock_adapter_descriptor();
    ns->adapter->teardown = NULL;
    ctx = ntt_create(ns->adapter, ns->cfg);
    assert_null(ctx);
}

/** @brief A reject from the adapter's validate_modulus aborts creation. */
static void torture_ntt_create_rejects_validate_modulus(void **state)
{
    ntt_state *ns = *state;

    mock.fail_validate = true;
    ntt_ctx *ctx = ntt_create(ns->adapter, ns->cfg);
    assert_null(ctx);
    assert_int_equal(mock.validate_calls, 1);
    assert_int_equal(mock.setup_calls, 0);
    assert_int_equal(mock.teardown_calls, 0);
}

/** @brief Flags unsupported by the adapter are rejected. */
static void torture_ntt_create_rejects_unsupported_flags(void **state)
{
    ntt_state *ns = *state;
    int rc;

    rc = ntt_config_set_flags(ns->cfg, NTT_CONFIG_REDUCTION_BARRETT);
    assert_int_equal(rc, NTT_OK);
    ntt_ctx *ctx = ntt_create(ns->adapter, ns->cfg);
    assert_null(ctx);
}

/** @brief A failed adapter setup aborts creation without teardown. */
static void torture_ntt_create_setup_failure(void **state)
{
    ntt_state *ns = *state;

    mock.fail_setup = true;
    ntt_ctx *ctx = ntt_create(ns->adapter, ns->cfg);
    assert_null(ctx);
    assert_int_equal(mock.validate_calls, 1);
    assert_int_equal(mock.setup_calls, 1);
    assert_int_equal(mock.teardown_calls, 0);
}

/** @brief A successful mock adapter creation wires the descriptor in. */
static void torture_ntt_create_mock_success(void **state)
{
    ntt_state *ns = *state;

    ntt_ctx *ctx = ntt_create(ns->adapter, ns->cfg);
    assert_non_null(ctx);
    assert_int_equal(mock.validate_calls, 1);
    assert_int_equal(mock.setup_calls, 1);
    assert_int_equal(mock.teardown_calls, 0);
    assert_int_equal(ctx->q, TEST_Q);
    assert_int_equal(ctx->n, TEST_N);
    assert_ptr_equal(ctx->adapter, ns->adapter);
    assert_ptr_equal(ctx->state, mock.setup_state);

    ntt_destroy(ctx);
}

/** @brief Missing roots are derived without mutating the caller's config. */
static void torture_ntt_create_leaves_config_untouched(void **state)
{
    ntt_state *ns = *state;

    ntt_ctx *ctx = ntt_create(ns->adapter, ns->cfg);
    assert_non_null(ctx);
    uint64_t omega = ntt_config_get_omega(ns->cfg);
    uint64_t psi = ntt_config_get_psi(ns->cfg);
    assert_int_equal(omega, 0);
    assert_int_equal(psi, 0);

    ntt_destroy(ctx);
}

/** @brief Distinct contexts coexist on the mock adapter. */
static void torture_ntt_create_multiple_adapters(void **state)
{
    ntt_state *ns = *state;
    ntt_adapter other = mock_adapter_descriptor();

    ntt_ctx *c1 = ntt_create(ns->adapter, ns->cfg);
    ntt_ctx *c2 = ntt_create(&other, ns->cfg);
    assert_non_null(c1);
    assert_non_null(c2);
    assert_ptr_not_equal(c1, c2);

    ntt_destroy(c1);
    ntt_destroy(c2);
}

/** @brief ntt_destroy tolerates a NULL context. */
static void torture_ntt_destroy_null_safe(void **state)
{
    (void)state;
    ntt_destroy(NULL);
}

/** @brief ntt_destroy hands the adapter state to the teardown callback. */
static void torture_ntt_destroy_invokes_teardown(void **state)
{
    ntt_state *ns = *state;

    ntt_ctx *ctx = ntt_create(ns->adapter, ns->cfg);
    assert_non_null(ctx);
    assert_int_equal(mock.teardown_calls, 0);
    assert_non_null(mock.setup_state);

    ntt_destroy(ctx);
    assert_int_equal(mock.teardown_calls, 1);
    assert_null(mock.setup_state);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(torture_ntt_create_null_adapter,
                                        setup_ntt_state,
                                        teardown_ntt_state),
        cmocka_unit_test_setup_teardown(torture_ntt_create_null_config,
                                        setup_ntt_state,
                                        teardown_ntt_state),
        cmocka_unit_test_setup_teardown(torture_ntt_create_null_arguments,
                                        setup_ntt_state,
                                        teardown_ntt_state),
        cmocka_unit_test_setup_teardown(torture_ntt_create_rejects_newer_abi,
                                        setup_ntt_state,
                                        teardown_ntt_state),
        cmocka_unit_test_setup_teardown(torture_ntt_create_rejects_small_struct,
                                        setup_ntt_state,
                                        teardown_ntt_state),
        cmocka_unit_test_setup_teardown(torture_ntt_create_accepts_older_abi,
                                        setup_ntt_state,
                                        teardown_ntt_state),
        cmocka_unit_test_setup_teardown(
            torture_ntt_create_rejects_missing_callbacks,
            setup_ntt_state,
            teardown_ntt_state),
        cmocka_unit_test_setup_teardown(
            torture_ntt_create_rejects_validate_modulus,
            setup_ntt_state,
            teardown_ntt_state),
        cmocka_unit_test_setup_teardown(
            torture_ntt_create_rejects_unsupported_flags,
            setup_ntt_state,
            teardown_ntt_state),
        cmocka_unit_test_setup_teardown(torture_ntt_create_setup_failure,
                                        setup_ntt_state,
                                        teardown_ntt_state),
        cmocka_unit_test_setup_teardown(torture_ntt_create_mock_success,
                                        setup_ntt_state,
                                        teardown_ntt_state),
        cmocka_unit_test_setup_teardown(
            torture_ntt_create_leaves_config_untouched,
            setup_ntt_state,
            teardown_ntt_state),
        cmocka_unit_test_setup_teardown(torture_ntt_create_multiple_adapters,
                                        setup_ntt_state,
                                        teardown_ntt_state),
        cmocka_unit_test_setup_teardown(torture_ntt_destroy_null_safe,
                                        setup_ntt_state,
                                        teardown_ntt_state),
        cmocka_unit_test_setup_teardown(torture_ntt_destroy_invokes_teardown,
                                        setup_ntt_state,
                                        teardown_ntt_state),
    };

    ntt_test_set_log_level();
    return cmocka_run_group_tests(tests, NULL, NULL);
}
