#include <assert.h>

#include "exit_press.h"

int main(void)
{
    assert(exit_press_switches_mode(EXIT_PRESS_LONG));
    assert(!exit_press_switches_mode(EXIT_PRESS_SHORT));
    assert(exit_next_mode(false) == EXIT_WIFI_MODE_PROVISIONING);
    assert(exit_next_mode(true) == EXIT_WIFI_MODE_STA);
    return 0;
}
