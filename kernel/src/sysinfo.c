#include <sysinfo.h>
#include <sysinfo_values.h>

const sysinfo_t sysinfo = {
    .git_branch = SYSINFO_GIT_BRANCH,
    .git_commit = SYSINFO_GIT_COMMIT,
    .build_datetime = SYSINFO_BUILD_DATETIME,
    .build_label = SYSINFO_BUILD_LABEL,
    .kernel_version = SYSINFO_KERNEL_VERSION,
};
