#include "services/wifi_credentials.h"
#include "test_util.h"

static void test_add_rules(void)
{
    wifi_credential_list_t list;
    wifi_credentials_clear(&list);
    CHECK(list.count == 0);

    CHECK(wifi_credentials_add(&list, "Home", "secret") == true);
    CHECK(wifi_credentials_add(&list, "Work", "") == true);   /* open ok */
    CHECK(list.count == 2);

    /* Empty and NULL SSIDs are ignored. */
    CHECK(wifi_credentials_add(&list, "", "x") == false);
    CHECK(wifi_credentials_add(&list, NULL, "x") == false);

    /* Duplicate SSID is ignored. */
    CHECK(wifi_credentials_add(&list, "Home", "other") == false);
    CHECK(list.count == 2);

    /* Third fits, fourth overflows the fixed capacity. */
    CHECK(wifi_credentials_add(&list, "Cafe", "beans") == true);
    CHECK(wifi_credentials_add(&list, "Extra", "nope") == false);
    CHECK(list.count == WIFI_MAX_NETWORKS);
}

static void test_add_length_limits(void)
{
    wifi_credential_list_t list;
    wifi_credentials_clear(&list);

    char long_ssid[WIFI_SSID_CAPACITY + 4];
    for (size_t i = 0; i < sizeof(long_ssid) - 1; ++i) { long_ssid[i] = 'A'; }
    long_ssid[sizeof(long_ssid) - 1] = '\0';
    CHECK(wifi_credentials_add(&list, long_ssid, "p") == false);

    char long_pass[WIFI_PASSWORD_CAPACITY + 4];
    for (size_t i = 0; i < sizeof(long_pass) - 1; ++i) { long_pass[i] = 'x'; }
    long_pass[sizeof(long_pass) - 1] = '\0';
    CHECK(wifi_credentials_add(&list, "Net", long_pass) == false);
    CHECK(list.count == 0);
}

static void test_match_priority(void)
{
    wifi_credential_list_t list;
    wifi_credentials_clear(&list);
    wifi_credentials_add(&list, "Home", "a");   /* priority 0 */
    wifi_credentials_add(&list, "Work", "b");   /* priority 1 */
    wifi_credentials_add(&list, "Cafe", "c");   /* priority 2 */

    /* Only Work is in range. */
    const char *scan1[] = {"Neighbour", "Work", "Guest"};
    CHECK(wifi_credentials_match(&list, scan1, 3) == 1);

    /* Home and Cafe both present -> Home wins on priority order. */
    const char *scan2[] = {"Cafe", "Home"};
    CHECK(wifi_credentials_match(&list, scan2, 2) == 0);

    /* None of the known networks are around. */
    const char *scan3[] = {"Random", "Other"};
    CHECK(wifi_credentials_match(&list, scan3, 2) == -1);

    /* Empty scan and NULL entries are handled safely. */
    CHECK(wifi_credentials_match(&list, scan3, 0) == -1);
    const char *scan4[] = {NULL, "Work"};
    CHECK(wifi_credentials_match(&list, scan4, 2) == 1);
}

int main(void)
{
    test_add_rules();
    test_add_length_limits();
    test_match_priority();
    TEST_REPORT("wifi_credentials");
}
