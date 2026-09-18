#ifndef TASK_PRIVATE_H
#define TASK_PRIVATE_H

// intentionally omits #include <stdint.h>
// mimics embedded C private headers that rely on the TU
// to include dependencies before including this header

struct TaskContext {
  uint32_t  taskId;
  uint8_t   priority;
  uint16_t  stackSize;
  uint32_t  flags;
};

#endif
