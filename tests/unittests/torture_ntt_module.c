#define _POSIX_C_SOURCE 200809L

#include "ntt_module.c"
#include "test_common.h"

/*
 * Stubs replacing the library services that ntt_module.c delegates to, so the
 * module loader, core handshake, and default-adapter reset can be controlled
 * from the tests.
 */
static ntt_adapter mock_full = {
    .abi_version = NTT_ADAPTER_ABI_VERSION,
    .struct_size = sizeof(ntt_adapter),
    .name = "mock_full",
};

static ntt_adapter mock_minimal = {
    .abi_version = NTT_ADAPTER_ABI_VERSION,
    .struct_size = sizeof(ntt_adapter),
    .name = "mock_minimal",
};

const ntt_adapter *ntt_adapter_scalar(void)
{
    return &mock_full;
}

const ntt_adapter *ntt_adapter_scalar_toy(void)
{
    return &mock_minimal;
}

/* Result of the ntt__adapter_is_compatible() stub. */
static bool mock_compatible = true;

bool ntt__adapter_is_compatible(const ntt_adapter *adapter)
{
    (void)adapter;
    return mock_compatible;
}

static ntt_dispatch mock_dispatch[] = {
    {
        NTT_FUNC_NONE,
        NULL,
    },
};

static ntt_core_api mock_core = {
    .version = NTT_CORE_API_VERSION,
    .struct_size = sizeof(ntt_core_api),
    .ops = mock_dispatch,
};

/* Core returned by the ntt__core_api() stub; NULL simulates a broken core. */
static const ntt_core_api *mock_core_ptr = &mock_core;

const ntt_core_api *ntt__core_api(void)
{
    return mock_core_ptr;
}

/* Dynamic-loader shim state. */
static void *mock_dlopen_result = NULL;
static char mock_dlopen_path[1024];
static void *mock_dlsym_result = NULL;
static char mock_dlsym_name[128];
static int mock_dlclose_calls = 0;

void *ntt__dlopen(const char *path)
{
    (void)snprintf(mock_dlopen_path, sizeof(mock_dlopen_path), "%s", path);
    return mock_dlopen_result;
}

void *ntt__dlsym(void *handle, const char *name)
{
    (void)handle;
    (void)snprintf(mock_dlsym_name, sizeof(mock_dlsym_name), "%s", name);
    return mock_dlsym_result;
}

void ntt__dlclose(void *handle)
{
    (void)handle;
    ++mock_dlclose_calls;
}

const char *ntt__dl_extension(void)
{
#if defined(_WIN32)
    return ".dll";
#else
    return ".so";
#endif
}

const char *ntt__dl_prefix(void)
{
#if defined(_WIN32)
    return "";
#else
    return "lib";
#endif
}

const char *ntt__dl_separator(void)
{
#if defined(_WIN32)
    return "\\";
#else
    return "/";
#endif
}

/* Descriptor handed out by a successful module entry point. */
static ntt_adapter mock_module_adapter = {
    .abi_version = NTT_ADAPTER_ABI_VERSION,
    .struct_size = sizeof(ntt_adapter),
    .name = "mock_module",
};

/* Module entry points. */
static int mock_entry_ok(const ntt_core_api *core, const ntt_adapter **out)
{
    (void)core;
    *out = &mock_module_adapter;
    return 0;
}

static int mock_entry_error(const ntt_core_api *core, const ntt_adapter **out)
{
    (void)core;
    (void)out;
    return NTT_ERROR;
}

static int mock_entry_null(const ntt_core_api *core, const ntt_adapter **out)
{
    (void)core;
    *out = NULL;
    return 0;
}

/* Helpers for the dynamic-loader stubs. */
static void mock_set_dlsym_entry(int (*fn)(const ntt_core_api *,
                                           const ntt_adapter **))
{
    union {
        void *obj;
        int (*fn)(const ntt_core_api *, const ntt_adapter **);
    } u;
    u.fn = fn;
    mock_dlsym_result = u.obj;
}

static void reset_mocks(void)
{
    for (size_t i = 0; i < NTT_MAX_LOADED_MODULES; ++i) {
        ntt__loaded[i].handle = NULL;
    }
    mock_compatible = true;
    mock_core_ptr = &mock_core;
    mock_core.version = NTT_CORE_API_VERSION;
    mock_core.struct_size = sizeof(ntt_core_api);
    mock_core.ops = mock_dispatch;
    mock_dlopen_result = NULL;
    mock_dlopen_path[0] = '\0';
    mock_dlsym_result = NULL;
    mock_dlsym_name[0] = '\0';
    mock_dlclose_calls = 0;
}

/** @brief ntt__registry_lookup: NULL, built-ins and unknown names. */
static void torture_ntt_module_registry_lookup(void **state)
{
    (void)state;
    const ntt_adapter *adapter = NULL;
    reset_mocks();

    adapter = ntt__registry_lookup(NULL);
    assert_null(adapter);

    adapter = ntt__registry_lookup("scalar");
    assert_ptr_equal(adapter, &mock_full);

    adapter = ntt__registry_lookup("scalar_toy");
    assert_ptr_equal(adapter, &mock_minimal);

    adapter = ntt__registry_lookup("no_such_adapter");
    assert_null(adapter);

    adapter = ntt__registry_lookup("");
    assert_null(adapter);
}

/** @brief ntt__module_cache_handle: caching, capacity and re-use. */
static void torture_ntt_module_cache_handle(void **state)
{
    (void)state;
    bool cached;
    reset_mocks();

    /* The registry is empty, so every slot is accepted. */
    for (size_t i = 0; i < NTT_MAX_LOADED_MODULES; ++i) {
        cached = ntt__module_cache_handle((void *)(uintptr_t)(i + 1));
        assert_true(cached);
        assert_ptr_equal(ntt__loaded[i].handle, (void *)(uintptr_t)(i + 1));
    }

    /* Registry full: no more handles can be cached. */
    cached = ntt__module_cache_handle((void *)0x1234);
    assert_false(cached);

    /* Freeing a slot makes caching possible again. */
    ntt__loaded[0].handle = NULL;
    cached = ntt__module_cache_handle((void *)0x5678);
    assert_true(cached);
    assert_ptr_equal(ntt__loaded[0].handle, (void *)0x5678);
}

/** @brief ntt__module_core_is_valid: handshake field checks. */
static void torture_ntt_module_core_is_valid(void **state)
{
    (void)state;
    const size_t ops_field_end =
        offsetof(ntt_core_api, ops) + sizeof(ntt_dispatch *);
    reset_mocks();

    assert_true(ntt__module_core_is_valid());

    /* NULL core is rejected. */
    mock_core_ptr = NULL;
    assert_false(ntt__module_core_is_valid());
    mock_core_ptr = &mock_core;

    /* A newer core version is rejected. */
    mock_core.version = NTT_CORE_API_VERSION + 1;
    assert_false(ntt__module_core_is_valid());
    mock_core.version = NTT_CORE_API_VERSION;

    /* A struct_size that does not cover the ops pointer is rejected. */
    mock_core.struct_size = (uint32_t)(ops_field_end - 1);
    assert_false(ntt__module_core_is_valid());

    /* Exactly covering the ops field is accepted. */
    mock_core.struct_size = (uint32_t)ops_field_end;
    assert_true(ntt__module_core_is_valid());
    mock_core.struct_size = sizeof(ntt_core_api);

    /* A NULL dispatch stream is rejected. */
    mock_core.ops = NULL;
    assert_false(ntt__module_core_is_valid());
    mock_core.ops = mock_dispatch;
}

/** @brief ntt__module_load_from_dir: every load/validate failure path. */
static void torture_ntt_module_load_from_dir(void **state)
{
    (void)state;
    const ntt_adapter *adapter = NULL;
    const char *module_dir = "/tmp/ntt/adapters";
    const char *name = "ext";
    reset_mocks();

    /* dlopen fails: nothing to validate, no close is needed. */
    mock_dlopen_result = NULL;
    adapter = ntt__module_load_from_dir(module_dir, name);
    assert_null(adapter);
    assert_int_equal(mock_dlclose_calls, 0);

    /* dlopen succeeds but the entry symbol is missing. */
    mock_dlopen_result = (void *)0x1;
    mock_dlsym_result = NULL;
    adapter = ntt__module_load_from_dir(module_dir, name);
    assert_null(adapter);
    assert_int_equal(mock_dlclose_calls, 1);

    /* Entry symbol present but the injected core is malformed. */
    mock_set_dlsym_entry(mock_entry_ok);
    mock_core_ptr = NULL;
    adapter = ntt__module_load_from_dir(module_dir, name);
    assert_null(adapter);
    assert_int_equal(mock_dlclose_calls, 2);
    mock_core_ptr = &mock_core;

    /* Entry returns an error. */
    mock_set_dlsym_entry(mock_entry_error);
    adapter = ntt__module_load_from_dir(module_dir, name);
    assert_null(adapter);
    assert_int_equal(mock_dlclose_calls, 3);

    /* Entry succeeds but leaves the adapter NULL. */
    mock_set_dlsym_entry(mock_entry_null);
    adapter = ntt__module_load_from_dir(module_dir, name);
    assert_null(adapter);
    assert_int_equal(mock_dlclose_calls, 4);

    /* Adapter is not compatible with the current ABI. */
    mock_set_dlsym_entry(mock_entry_ok);
    mock_compatible = false;
    adapter = ntt__module_load_from_dir(module_dir, name);
    assert_null(adapter);
    assert_int_equal(mock_dlclose_calls, 5);
    mock_compatible = true;
}

/** @brief ntt__module_load_from_dir: successful load and handle caching. */
static void torture_ntt_module_load_from_dir_success(void **state)
{
    (void)state;
    const ntt_adapter *adapter = NULL;
    const char *module_dir = "/tmp/ntt/adapters";
    const char *name = "ext";
    reset_mocks();

    mock_dlopen_result = (void *)0x1;
    mock_set_dlsym_entry(mock_entry_ok);

    adapter = ntt__module_load_from_dir(module_dir, name);
    assert_ptr_equal(adapter, &mock_module_adapter);

    /* The requested library path and symbol are used. */
#if defined(_WIN32)
    assert_string_equal(mock_dlopen_path,
                        "/tmp/ntt/adapters\\ntt_adapter_ext.dll");
#else
    assert_string_equal(mock_dlopen_path,
                        "/tmp/ntt/adapters/libntt_adapter_ext.so");
#endif
    assert_string_equal(mock_dlsym_name, NTT_ADAPTER_MODULE_ENTRY);

    /* The handle is kept resident and not closed. */
    assert_int_equal(mock_dlclose_calls, 0);
    assert_ptr_equal(ntt__loaded[0].handle, (void *)0x1);
}

/** @brief ntt__module_load_from_dir: registry full -> handle is closed. */
static void torture_ntt_module_load_from_dir_cache_full(void **state)
{
    (void)state;
    const ntt_adapter *adapter = NULL;
    reset_mocks();

    for (size_t i = 0; i < NTT_MAX_LOADED_MODULES; ++i) {
        ntt__loaded[i].handle = (void *)(uintptr_t)(i + 1);
    }

    mock_dlopen_result = (void *)0x2;
    mock_set_dlsym_entry(mock_entry_ok);

    /* 
     * The adapter is still handed out, but the handle is closed because it
     * cannot be cached.
     */
    adapter = ntt__module_load_from_dir("/tmp/ntt/adapters", "ext");
    assert_ptr_equal(adapter, &mock_module_adapter);
    assert_int_equal(mock_dlclose_calls, 1);
}

/** @brief ntt__module_unload_all: closes cached handles and clears them. */
static void torture_ntt_module_unload_all(void **state)
{
    (void)state;
    reset_mocks();

    /* Nothing loaded: still a safe no-op. */
    ntt__module_unload_all();
    assert_int_equal(mock_dlclose_calls, 0);

    /* With cached handles, each one is closed and cleared. */
    for (size_t i = 0; i < NTT_MAX_LOADED_MODULES; ++i) {
        ntt__loaded[i].handle = (void *)(uintptr_t)(i + 1);
    }
    ntt__module_unload_all();
    assert_int_equal(mock_dlclose_calls, NTT_MAX_LOADED_MODULES);
    for (size_t i = 0; i < NTT_MAX_LOADED_MODULES; ++i) {
        assert_null(ntt__loaded[i].handle);
    }
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(torture_ntt_module_registry_lookup),
        cmocka_unit_test(torture_ntt_module_cache_handle),
        cmocka_unit_test(torture_ntt_module_core_is_valid),
        cmocka_unit_test(torture_ntt_module_load_from_dir),
        cmocka_unit_test(torture_ntt_module_load_from_dir_success),
        cmocka_unit_test(torture_ntt_module_load_from_dir_cache_full),
        cmocka_unit_test(torture_ntt_module_unload_all),
    };

    ntt_test_set_log_level();
    return cmocka_run_group_tests(tests, NULL, NULL);
}
