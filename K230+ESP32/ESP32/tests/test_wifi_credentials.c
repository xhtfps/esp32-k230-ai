#include <assert.h>
#include <string.h>

#include "wifi_credentials.h"

int main(void)
{
    char ssid_32[33];
    char password_63[64];
    char ssid_33[34];
    char password_64[65];

    memset(ssid_32, 's', 32);
    ssid_32[32] = '\0';
    memset(password_63, 'p', 63);
    password_63[63] = '\0';
    memset(ssid_33, 's', 33);
    ssid_33[33] = '\0';
    memset(password_64, 'p', 64);
    password_64[64] = '\0';

    assert(!wifi_credentials_are_valid("", "password"));
    assert(wifi_credentials_are_valid(ssid_32, password_63));
    assert(!wifi_credentials_are_valid(ssid_33, "password"));
    assert(!wifi_credentials_are_valid("ssid", password_64));
    return 0;
}
