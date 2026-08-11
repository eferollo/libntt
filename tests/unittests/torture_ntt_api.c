#include "ntt_api.c"
#include "test_common.h"

/* Tracks the mock adapter's callback invocations and failure toggle. */
typedef struct {
    bool fail_calls; /* make the NTT callbacks return NTT_ERROR */
    size_t forward_calls;
    size_t inverse_calls;
    size_t mul_calls;
    void *last_state; /* adapter state passed to the last callback */
} mock_counters;

static mock_counters mock;

/* Per-test state carried through the cmocka state variable. */
typedef struct {
    ntt_adapter *adapter; /* mock adapter with the NTT callbacks installed */
    ntt_ctx ctx;          /* context backed by the mock adapter */
    void *ctx_state;      /* sentinel adapter state  */
    uint64_t a[4];
    uint64_t b[4];
    uint64_t c[4];
} ntt_state;

static int mock_adapter_forward(void *state, uint64_t *a)
{
    (void)a;
    mock.forward_calls++;
    mock.last_state = state;
    return mock.fail_calls ? NTT_ERROR : NTT_OK;
}

static int mock_adapter_inverse(void *state, uint64_t *a)
{
    (void)a;
    mock.inverse_calls++;
    mock.last_state = state;
    return mock.fail_calls ? NTT_ERROR : NTT_OK;
}

static int
mock_adapter_negacyclic_mul(void *state, uint64_t *a, uint64_t *b, uint64_t *c)
{
    (void)a;
    (void)b;
    (void)c;
    mock.mul_calls++;
    mock.last_state = state;
    return mock.fail_calls ? NTT_ERROR : NTT_OK;
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
    a.validate_modulus = NULL;
    a.setup = NULL;
    a.teardown = NULL;
    a.forward = mock_adapter_forward;
    a.inverse = mock_adapter_inverse;
    a.negacyclic_mul = mock_adapter_negacyclic_mul;
    return a;
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

    ns->ctx_state = calloc(1, sizeof(char));
    assert_non_null(ns->ctx_state);

    ns->ctx.q = UINT64_C(12289);
    ns->ctx.n = 256;
    ns->ctx.adapter = ns->adapter;
    ns->ctx.state = ns->ctx_state;

    *state = ns;
    return 0;
}

/** @brief Releases the test state installed by setup_ntt_state(). */
static int teardown_ntt_state(void **state)
{
    ntt_state *ns = *state;

    free(ns->ctx_state);
    free(ns->adapter);
    free(ns);
    return 0;
}

/** @brief ntt_forward dispatches to the adapter forward callback. */
static void torture_ntt_api_forward_dispatches(void **state)
{
    ntt_state *ns = *state;

    int rc = ntt_forward(&ns->ctx, ns->a);
    assert_int_equal(rc, NTT_OK);
    assert_int_equal(mock.forward_calls, 1);
    assert_ptr_equal(mock.last_state, ns->ctx_state);
}

/** @brief ntt_inverse dispatches to the adapter inverse callback. */
static void torture_ntt_api_inverse_dispatches(void **state)
{
    ntt_state *ns = *state;

    int rc = ntt_inverse(&ns->ctx, ns->a);
    assert_int_equal(rc, NTT_OK);
    assert_int_equal(mock.inverse_calls, 1);
    assert_ptr_equal(mock.last_state, ns->ctx_state);
}

/** @brief ntt_negacyclic_mul dispatches to the adapter mul callback. */
static void torture_ntt_api_negacyclic_mul_dispatches(void **state)
{
    ntt_state *ns = *state;

    int rc = ntt_negacyclic_mul(ns->a, ns->b, ns->c, &ns->ctx);
    assert_int_equal(rc, NTT_OK);
    assert_int_equal(mock.mul_calls, 1);
    assert_ptr_equal(mock.last_state, ns->ctx_state);
}

/** @brief ntt_error returned by the adapter propagates unchanged. */
static void torture_ntt_api_callback_error_propagated(void **state)
{
    ntt_state *ns = *state;
    int rc;

    mock.fail_calls = true;
    rc = ntt_forward(&ns->ctx, ns->a);
    assert_int_equal(rc, NTT_ERROR);
    rc = ntt_inverse(&ns->ctx, ns->a);
    assert_int_equal(rc, NTT_ERROR);
    rc = ntt_negacyclic_mul(ns->a, ns->b, ns->c, &ns->ctx);
    assert_int_equal(rc, NTT_ERROR);
}

/** @brief ntt_forward rejects NULL arguments, callback or adapter state. */
static void torture_ntt_api_forward_invalid(void **state)
{
    ntt_state *ns = *state;
    int rc;

    rc = ntt_forward(NULL, ns->a);
    assert_int_equal(rc, NTT_ERROR);
    rc = ntt_forward(&ns->ctx, NULL);
    assert_int_equal(rc, NTT_ERROR);

    ns->ctx.adapter = NULL;
    rc = ntt_forward(&ns->ctx, ns->a);
    assert_int_equal(rc, NTT_ERROR);
    ns->ctx.adapter = ns->adapter;

    ns->adapter->forward = NULL;
    rc = ntt_forward(&ns->ctx, ns->a);
    assert_int_equal(rc, NTT_ERROR);
    ns->adapter->forward = mock_adapter_forward;

    ns->ctx.state = NULL;
    rc = ntt_forward(&ns->ctx, ns->a);
    assert_int_equal(rc, NTT_ERROR);
    ns->ctx.state = ns->ctx_state;
}

/** @brief ntt_inverse rejects NULL arguments, callback or adapter state. */
static void torture_ntt_api_inverse_invalid(void **state)
{
    ntt_state *ns = *state;
    int rc;

    rc = ntt_inverse(NULL, ns->a);
    assert_int_equal(rc, NTT_ERROR);
    rc = ntt_inverse(&ns->ctx, NULL);
    assert_int_equal(rc, NTT_ERROR);

    ns->ctx.adapter = NULL;
    rc = ntt_inverse(&ns->ctx, ns->a);
    assert_int_equal(rc, NTT_ERROR);
    ns->ctx.adapter = ns->adapter;

    ns->adapter->inverse = NULL;
    rc = ntt_inverse(&ns->ctx, ns->a);
    assert_int_equal(rc, NTT_ERROR);
    ns->adapter->inverse = mock_adapter_inverse;

    ns->ctx.state = NULL;
    rc = ntt_inverse(&ns->ctx, ns->a);
    assert_int_equal(rc, NTT_ERROR);
    ns->ctx.state = ns->ctx_state;
}

/** @brief ntt_negacyclic_mul rejects NULL arguments, callback or state. */
static void torture_ntt_api_negacyclic_mul_invalid(void **state)
{
    ntt_state *ns = *state;
    int rc;

    rc = ntt_negacyclic_mul(NULL, ns->b, ns->c, &ns->ctx);
    assert_int_equal(rc, NTT_ERROR);
    rc = ntt_negacyclic_mul(ns->a, NULL, ns->c, &ns->ctx);
    assert_int_equal(rc, NTT_ERROR);
    rc = ntt_negacyclic_mul(ns->a, ns->b, NULL, &ns->ctx);
    assert_int_equal(rc, NTT_ERROR);
    rc = ntt_negacyclic_mul(ns->a, ns->b, ns->c, NULL);
    assert_int_equal(rc, NTT_ERROR);

    ns->ctx.adapter = NULL;
    rc = ntt_negacyclic_mul(ns->a, ns->b, ns->c, &ns->ctx);
    assert_int_equal(rc, NTT_ERROR);
    ns->ctx.adapter = ns->adapter;

    ns->adapter->negacyclic_mul = NULL;
    rc = ntt_negacyclic_mul(ns->a, ns->b, ns->c, &ns->ctx);
    assert_int_equal(rc, NTT_ERROR);
    ns->adapter->negacyclic_mul = mock_adapter_negacyclic_mul;

    ns->ctx.state = NULL;
    rc = ntt_negacyclic_mul(ns->a, ns->b, ns->c, &ns->ctx);
    assert_int_equal(rc, NTT_ERROR);
    ns->ctx.state = ns->ctx_state;
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(torture_ntt_api_forward_dispatches,
                                        setup_ntt_state,
                                        teardown_ntt_state),
        cmocka_unit_test_setup_teardown(torture_ntt_api_inverse_dispatches,
                                        setup_ntt_state,
                                        teardown_ntt_state),
        cmocka_unit_test_setup_teardown(
            torture_ntt_api_negacyclic_mul_dispatches,
            setup_ntt_state,
            teardown_ntt_state),
        cmocka_unit_test_setup_teardown(
            torture_ntt_api_callback_error_propagated,
            setup_ntt_state,
            teardown_ntt_state),
        cmocka_unit_test_setup_teardown(torture_ntt_api_forward_invalid,
                                        setup_ntt_state,
                                        teardown_ntt_state),
        cmocka_unit_test_setup_teardown(torture_ntt_api_inverse_invalid,
                                        setup_ntt_state,
                                        teardown_ntt_state),
        cmocka_unit_test_setup_teardown(torture_ntt_api_negacyclic_mul_invalid,
                                        setup_ntt_state,
                                        teardown_ntt_state),
    };

    ntt_test_set_log_level();
    return cmocka_run_group_tests(tests, NULL, NULL);
}
