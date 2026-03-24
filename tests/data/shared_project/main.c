#include "utils.h"
#include "header_only.h"

int main(void)
{
  struct Util util;
  init_util(1.0, 2, 'x', 3, 4.0, 'y');

  struct Config config;
  init_config(2.0, "test_dir", 'z', 1);

  struct HeaderOnlyStruct hos;
  hos.bigValue = 12345L;
  hos.smallValue = 'a';
  hos.mediumValue = 42;

  return 0;
}
