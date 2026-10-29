#ifndef NOTIFY_H
#define NOTIFY_H

#include "app.h"

BOOL NotifyInit(HINSTANCE hinst);
void NotifyShow(const wchar_t* text);
void NotifyHide(void);
void NotifyShutdown(void);

#endif /* NOTIFY_H */
