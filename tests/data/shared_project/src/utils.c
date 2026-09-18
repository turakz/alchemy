#include "utils.h"

int init_util(double d, int i, char c, int ii, double dd, char cc)
{
  struct Util util;
  util.someDouble = d;
  util.someInt = i;
  util.someChar = c;
  util.anotherInt = ii;
  util.anotherDouble = dd;
  return 0;
}
