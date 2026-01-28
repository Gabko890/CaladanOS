#ifndef SYSINFO_H
#define SYSINFO_H

typedef struct {
    const char *git_branch;
    const char *git_commit;
    const char *build_datetime;
    const char *build_label;
    const char *kernel_version;
} sysinfo_t;

extern const sysinfo_t sysinfo;

#endif // SYSINFO_H
