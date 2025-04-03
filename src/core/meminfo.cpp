#include "meminfo.h"

#include "clogger/clogger.h"

void PrintMemInfo() {
  multi_heap_info_t info;

  // 查询内部内存信息
  heap_caps_get_info(&info, MALLOC_CAP_INTERNAL);
  CLOG("############################Internal Memory:");
  CLOG("Free: %zu bytes", info.total_free_bytes);
  CLOG("Allocated: %zu bytes", info.total_allocated_bytes);
  CLOG("Minimum Free: %zu bytes", info.minimum_free_bytes);

  // 查询IRAM信息
  heap_caps_get_info(&info, MALLOC_CAP_EXEC);
  CLOG("IRAM:");
  CLOG("Free: %zu bytes", info.total_free_bytes);
  CLOG("Allocated: %zu bytes", info.total_allocated_bytes);
  CLOG("Minimum Free: %zu bytes", info.minimum_free_bytes);
}