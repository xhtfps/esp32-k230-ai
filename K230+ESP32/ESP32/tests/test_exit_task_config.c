#include <assert.h>

#include "exit_task_config.h"

int main(void)
{
    assert(EXIT_TASK_STACK_SIZE >= 6144U);
    return 0;
}
