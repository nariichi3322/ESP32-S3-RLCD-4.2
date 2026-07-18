// 声明启动联网预算、同步流程和任务入口。
#pragma once

int boot_sync_remaining_ms();
void run_boot_connectivity_sync();
void boot_connectivity_task(void *);
