#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "vision_state.h"

int main(void)
{
    assert(VISION_STATE_MAX_JPEG == 256U * 1024U);
    assert(VISION_STATE_JPEG_CHUNK_HDR == 6U);
    assert(VISION_STATE_JPEG_DONE_SIZE == 10U);
    assert(vision_state_process_frame(NULL) != ESP_OK);
    return 0;
}
