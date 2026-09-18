#ifndef UTILS_H
#define UTILS_H

#include "config.h"

struct Util {
  struct Config config;
  double someDouble;
  int someInt;
  char someChar;
  int anotherInt;
  double anotherDouble;
  char charArray[7];
};

int init_util(double, int, char, int, double, char);

#endif
