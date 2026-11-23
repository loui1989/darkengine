#ifndef DARK_RENDER_EVENT_BRIDGE_H
#define DARK_RENDER_EVENT_BRIDGE_H

bool DarkRenderBeginEventPump(void);
void DarkRenderWindowResized(void);
void DarkRenderWindowMinimized(void);
void DarkRenderWindowRestored(void);
void DarkRenderApplicationSuspended(void);
void DarkRenderApplicationResumed(void);
void DarkRenderEndEventPump(void);
// A key for the renderer's own use (the path tracer's settings panel): true
// if it took it, and the game must not have it. scancode and mods: SDL's.
bool DarkRenderHandleKey(int scancode, int mods, bool down, bool repeat);

#endif
