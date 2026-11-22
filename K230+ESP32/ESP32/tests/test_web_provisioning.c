#include <assert.h>
#include <string.h>

#include "web_provisioning.h"

int main(void)
{
    wifi_credentials_t credentials = {0};

    assert(web_parse_provisioning_form("ssid=Lab+Network&password=secret%21", &credentials));
    assert(strcmp(credentials.ssid, "Lab Network") == 0);
    assert(strcmp(credentials.password, "secret!") == 0);
    assert(!web_parse_provisioning_form("password=secret", &credentials));
    return 0;
}
