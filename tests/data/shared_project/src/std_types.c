#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "std_types.h"

void init_std_types(struct StdTypesStruct* s)
{
  s->count = 0;
  s->flags = 0;
  s->active = false;
  s->id = 0;
  s->offset = 0;
}
