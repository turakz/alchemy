#ifndef STD_TYPES_H
#define STD_TYPES_H

// intentionally omits #include <stddef.h>, <stdint.h>, <stdbool.h>
// mimics embedded C headers that rely on the TU to include std headers
// before including this header

struct StdTypesStruct {
  size_t    count;
  uint8_t   flags;
  bool      active;
  uint32_t  id;
  int16_t   offset;
};

#endif
