#include "config.h"

void init_config(double s, const char* d, char l, int id)
{
  struct Config config;
  config.status = s;
  config.dir = d;
  config.label = l;
  config.sysId = id;
}
