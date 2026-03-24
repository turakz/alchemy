#include <stdint.h>
#include "task_private.h"

void init_task(struct TaskContext* ctx)
{
  ctx->taskId = 0;
  ctx->priority = 0;
  ctx->stackSize = 512;
  ctx->flags = 0;
}
