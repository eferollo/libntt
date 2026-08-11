#define _GNU_SOURCE

#include "ntt_cfg_file.c"
#include "test_common.h"

/** @brief Test path resolution and every env/buffer failure mode. */
static void torture_ntt_cfg_default_path(void **state)
{
    (void)state;
    char buf[128] = {0};
    char small[16] = {0};
    int rc;

    /* NTT_CONFIG_FILE wins over HOME when it fits the buffer. */
    setenv("NTT_CONFIG_FILE", "/tmp/my_ntt.conf", 1);
    setenv("HOME", "/ignored", 1);
    rc = ntt__config_default_path(buf, sizeof(buf));
    assert_true(rc);
    assert_string_equal(buf, "/tmp/my_ntt.conf");
    unsetenv("NTT_CONFIG_FILE");
    unsetenv("HOME");

    /* An empty NTT_CONFIG_FILE falls back to the HOME default. */
    setenv("NTT_CONFIG_FILE", "", 1);
    setenv("HOME", "/home/user", 1);
    rc = ntt__config_default_path(buf, sizeof(buf));
    assert_true(rc);
    assert_string_equal(buf, "/home/user/.config/libntt/ntt.conf");
    unsetenv("NTT_CONFIG_FILE");
    unsetenv("HOME");

    /* No NTT_CONFIG_FILE and no HOME cannot be resolved. */
    rc = ntt__config_default_path(buf, sizeof(buf));
    assert_false(rc);

    /* NTT_CONFIG_FILE that does not fit the buffer is rejected. */
    setenv("NTT_CONFIG_FILE", "1234567890abcdef", 1); /* len == cap */
    rc = ntt__config_default_path(small, sizeof(small));
    assert_false(rc);
    /* len == cap-1 fits. */
    setenv("NTT_CONFIG_FILE", "1234567890abcde", 1);
    rc = ntt__config_default_path(small, sizeof(small));
    assert_true(rc);
    assert_string_equal(small, "1234567890abcde");
    unsetenv("NTT_CONFIG_FILE");

    /* A HOME too long for the buffer is rejected. */
    setenv("HOME", "/home/this/name/is/way/too/long", 1);
    rc = ntt__config_default_path(small, sizeof(small));
    assert_false(rc);
    unsetenv("HOME");
}

/** @brief Test every parse_line path against the static parser. */
static void torture_ntt_cfg_parse_line(void **state)
{
    (void)state;
    char line_adapter[] = "adapter = test_adapter   \n";
    char line_module_dir[] = "module_dir=/opt/libntt/mod\n";
    char line_no_equals[] = "just a stray line";
    char line_unknown[] = "color = red";
    char line_empty[] = "adapter =";
    char line_fits[] = "adapter = abcdefg";          /* value len 7, cap 8  */
    char line_adapter_over[] = "adapter = abcdefgh"; /* value len 8, cap 8 */
    char line_md_over[] = "module_dir = abcdefgh";   /* value len 8, cap 8 */
    char adapter[32] = "sentinel";
    char module_dir[32] = "sentinel";
    char small_adapter[8] = {0};
    char small_module_dir[8] = {0};
    int rc;

    /* An adapter key is stored trimmed of surrounding whitespace. */
    strcpy(adapter, "sentinel");
    strcpy(module_dir, "sentinel");
    rc = ntt__config_parse_line(line_adapter,
                                1,
                                adapter,
                                sizeof(adapter),
                                module_dir,
                                sizeof(module_dir));
    assert_int_equal(rc, 0);
    assert_string_equal(adapter, "test_adapter");
    assert_string_equal(module_dir, "sentinel");

    /* A module_dir key is stored, leaving the adapter buffer untouched. */
    strcpy(adapter, "sentinel");
    strcpy(module_dir, "sentinel");
    rc = ntt__config_parse_line(line_module_dir,
                                1,
                                adapter,
                                sizeof(adapter),
                                module_dir,
                                sizeof(module_dir));
    assert_int_equal(rc, 0);
    assert_string_equal(adapter, "sentinel");
    assert_string_equal(module_dir, "/opt/libntt/mod");

    /* A line without '=' is ignored without touching the buffers. */
    strcpy(adapter, "sentinel");
    strcpy(module_dir, "sentinel");
    rc = ntt__config_parse_line(line_no_equals,
                                1,
                                adapter,
                                sizeof(adapter),
                                module_dir,
                                sizeof(module_dir));
    assert_int_equal(rc, 0);
    assert_string_equal(adapter, "sentinel");
    assert_string_equal(module_dir, "sentinel");

    /* An unrecognized key is ignored without touching the buffers. */
    strcpy(adapter, "sentinel");
    strcpy(module_dir, "sentinel");
    rc = ntt__config_parse_line(line_unknown,
                                1,
                                adapter,
                                sizeof(adapter),
                                module_dir,
                                sizeof(module_dir));
    assert_int_equal(rc, 0);
    assert_string_equal(adapter, "sentinel");
    assert_string_equal(module_dir, "sentinel");

    /* An empty value is accepted and stored as an empty string. */
    strcpy(adapter, "sentinel");
    strcpy(module_dir, "sentinel");
    rc = ntt__config_parse_line(line_empty,
                                1,
                                adapter,
                                sizeof(adapter),
                                module_dir,
                                sizeof(module_dir));
    assert_int_equal(rc, 0);
    assert_string_equal(adapter, "");
    assert_string_equal(module_dir, "sentinel");

    /* A value exactly at capacity-1 fits. */
    strcpy(module_dir, "sentinel");
    rc = ntt__config_parse_line(line_fits,
                                1,
                                small_adapter,
                                sizeof(small_adapter),
                                module_dir,
                                sizeof(module_dir));
    assert_int_equal(rc, 0);
    assert_string_equal(small_adapter, "abcdefg");
    assert_string_equal(module_dir, "sentinel");

    /* An adapter value at capacity fails. */
    strcpy(module_dir, "sentinel");
    rc = ntt__config_parse_line(line_adapter_over,
                                1,
                                small_adapter,
                                sizeof(small_adapter),
                                module_dir,
                                sizeof(module_dir));
    assert_int_equal(rc, -1);
    assert_string_equal(module_dir, "sentinel");

    /* A module_dir value at capacity fails, leaving the adapter untouched. */
    strcpy(adapter, "sentinel");
    rc = ntt__config_parse_line(line_md_over,
                                1,
                                adapter,
                                sizeof(adapter),
                                small_module_dir,
                                sizeof(small_module_dir));
    assert_int_equal(rc, -1);
    assert_string_equal(adapter, "sentinel");
}

/** @brief Writes content to a unique temporary file and returns its path. */
static char *write_cfg_file(const char *content)
{
    char template[] = "/tmp/ntt_cfg_unit_XXXXXX";
    int fd = mkstemp(template);
    assert_true(fd >= 0);

    FILE *fp = fdopen(fd, "w");
    assert_non_null(fp);
    if (content != NULL) {
        fputs(content, fp);
    }
    assert_int_equal(fclose(fp), 0);

    return strdup(template);
}

/** @brief Removes a file created by write_cfg_file() and frees the path. */
static void remove_cfg_file(char *path)
{
    if (path != NULL) {
        (void)remove(path);
        free(path);
    }
}

/** @brief Exercises every ntt__config_file_load path on real files. */
static void torture_ntt_cfg_file_load(void **state)
{
    (void)state;
    char adapter[32] = {0};
    char module_dir[64] = {0};
    char small_adapter[16] = {0};
    char small_module_dir[16] = {0};
    char value[32] = {0};
    char content[64] = {0};
    char *path = NULL;
    int n, rc;

    /* Bad arguments are rejected. */
    rc = ntt__config_file_load(NULL,
                               adapter,
                               sizeof(adapter),
                               module_dir,
                               sizeof(module_dir));
    assert_false(rc);
    rc = ntt__config_file_load("/tmp/whatever",
                               NULL,
                               sizeof(adapter),
                               module_dir,
                               sizeof(module_dir));
    assert_false(rc);
    rc = ntt__config_file_load("/tmp/whatever",
                               adapter,
                               sizeof(adapter),
                               NULL,
                               sizeof(module_dir));
    assert_false(rc);

    /* An unreadable file fails. */
    rc = ntt__config_file_load("/no/such/ntt.conf",
                               adapter,
                               sizeof(adapter),
                               module_dir,
                               sizeof(module_dir));
    assert_false(rc);

    /* Adapter and module_dir keys are stored, comments are skipped. */
    path = write_cfg_file("# global config\n"
                          "\n"
                          "adapter = test_adapter\n"
                          "module_dir = /opt/libntt\n");
    rc = ntt__config_file_load(path,
                               adapter,
                               sizeof(adapter),
                               module_dir,
                               sizeof(module_dir));
    assert_true(rc);
    assert_string_equal(adapter, "test_adapter");
    assert_string_equal(module_dir, "/opt/libntt");
    remove_cfg_file(path);

    /* Whitespace around keys and values is trimmed. */
    path = write_cfg_file("   adapter   =   montgomery   \n"
                          "  module_dir = /a/b  \n");
    rc = ntt__config_file_load(path,
                               adapter,
                               sizeof(adapter),
                               module_dir,
                               sizeof(module_dir));
    assert_true(rc);
    assert_string_equal(adapter, "montgomery");
    assert_string_equal(module_dir, "/a/b");
    remove_cfg_file(path);

    /* Files with only comments, blanks and unknown keys yield empties. */
    path = write_cfg_file("# only comments\n"
                          "\n"
                          "color = red\n"
                          "noise without equals\n");
    rc = ntt__config_file_load(path,
                               adapter,
                               sizeof(adapter),
                               module_dir,
                               sizeof(module_dir));
    assert_true(rc);
    assert_string_equal(adapter, "");
    assert_string_equal(module_dir, "");
    remove_cfg_file(path);

    /* An adapter value that overflows its buffer fails the whole load. */
    memset(value, 'x', sizeof(value) - 1);
    value[sizeof(value) - 1] = '\0';
    n = snprintf(content, sizeof(content), "adapter = %s\n", value);
    assert_true(n > 0 && (size_t)n < sizeof(content));
    path = write_cfg_file(content);
    rc = ntt__config_file_load(path,
                               small_adapter,
                               sizeof(small_adapter),
                               module_dir,
                               sizeof(module_dir));
    assert_false(rc);
    remove_cfg_file(path);

    /* A module_dir value that overflows its buffer fails the load. */
    memset(value, 'y', sizeof(value) - 1);
    value[sizeof(value) - 1] = '\0';
    n = snprintf(content, sizeof(content), "module_dir = %s\n", value);
    assert_true(n > 0 && (size_t)n < sizeof(content));
    path = write_cfg_file(content);
    rc = ntt__config_file_load(path,
                               adapter,
                               sizeof(adapter),
                               small_module_dir,
                               sizeof(small_module_dir));
    assert_false(rc);
    remove_cfg_file(path);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(torture_ntt_cfg_default_path),
        cmocka_unit_test(torture_ntt_cfg_parse_line),
        cmocka_unit_test(torture_ntt_cfg_file_load),
    };

    ntt_test_set_log_level();
    return cmocka_run_group_tests(tests, NULL, NULL);
}
