#include <assert.h>

#include "wifi_softap_profile.h"

int main(void)
{
    assert(WIFI_PROVISIONING_CHANNEL == 6U);
    assert(WIFI_PROVISIONING_PROTOCOLS == 0x07U);
    assert(WIFI_PROVISIONING_BEACON_INTERVAL == 100U);
    assert(!WIFI_PROVISIONING_SSID_HIDDEN);
    return 0;
}
