#include <assert.h>

#include "camera_preview_config.h"

int main(void)
{
    assert(CAMERA_XCLK_HZ == 20000000);
    assert(CAMERA_FRAME_BUFFER_COUNT == 3);
    assert(CAMERA_USE_LATEST_FRAME == 1);
    return 0;
}
