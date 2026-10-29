#ifndef SETTINGS_H
#define SETTINGS_H

#include "app.h"

BOOL SettingsInit(HINSTANCE hinst);
void SettingsShow(void);
HWND SettingsWindow(void);
void SettingsShutdown(void);

#endif /* SETTINGS_H */
