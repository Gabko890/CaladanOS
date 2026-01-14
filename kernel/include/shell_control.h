#ifndef SHELL_CONTROL_H
#define SHELL_CONTROL_H

// Control shell input processing from other subsystems (e.g., GUI)
void shell_pause(void);
void shell_resume(void);
int shell_is_active(void);

#endif // SHELL_CONTROL_H
