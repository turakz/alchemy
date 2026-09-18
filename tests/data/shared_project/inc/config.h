#ifndef CONFIG_H
#define CONFIG_H

struct Config {
  double status;
  const char* dir;
  char label;
  int sysId;
};

void init_config(double, const char*, char, int);

#endif
