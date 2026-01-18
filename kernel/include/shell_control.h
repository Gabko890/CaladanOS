#ifndef SHELL_CONTROL_H
#define SHELL_CONTROL_H

// Control shell input processing from other subsystems (e.g., GUI)
void shell_pause(void);
void shell_resume(void);
int shell_is_active(void);

// Temporary input capture mode used by external components (e.g., Lua VM)
void shell_capture_begin(void);
void shell_capture_end(void);
int shell_capture_is_ready(void);

#endif // SHELL_CONTROL_H
