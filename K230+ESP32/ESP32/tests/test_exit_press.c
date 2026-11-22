#include <assert.h>

#include "exit_press.h"

int main(void)
{
    assert(exit_classify_press(2999) == EXIT_PRESS_SHORT);
    assert(exit_classify_press(3000) == EXIT_PRESS_LONG);
    assert(!exit_press_switches_mode(EXIT_PRESS_SHORT));
    assert(exit_press_switches_mode(EXIT_PRESS_LONG));
    return 0;
}
