#pragma once

/* GPIO0 长按会在该任务内完成 Wi-Fi 停止、配置和启动，保留足够调用栈空间。 */
#define EXIT_TASK_STACK_SIZE 6144U
