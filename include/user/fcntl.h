#ifndef RIVERIX_USER_FCNTL_H
#define RIVERIX_USER_FCNTL_H

#include "shared/syscall_abi.h"

#define O_RDONLY SYS_O_RDONLY
#define O_WRONLY SYS_O_WRONLY
#define O_RDWR SYS_O_RDWR
#define O_CREATE SYS_O_CREATE
#define O_TRUNC SYS_O_TRUNC

#define SEEK_SET SYS_SEEK_SET
#define SEEK_CUR SYS_SEEK_CUR
#define SEEK_END SYS_SEEK_END

#endif
