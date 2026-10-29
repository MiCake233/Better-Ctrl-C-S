#ifndef CONFIG_H
#define CONFIG_H

#include "app.h"

void ConfigSetDefaults(Config* c);
BOOL ConfigLoad(Config* c, const wchar_t* path);
BOOL ConfigSave(const Config* c, const wchar_t* path);

#endif /* CONFIG_H */
