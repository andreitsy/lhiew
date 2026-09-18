#pragma once

#include <sys/stat.h>

/* Keep inode alias checks separate from edit/backup version checks: a resized
   hard link still aliases its source and must never become its backup. */
static inline int file_same_inode(const struct stat *a, const struct stat *b) {
    return a->st_dev == b->st_dev && a->st_ino == b->st_ino;
}

static inline int file_same_extent(const struct stat *a, const struct stat *b) {
    return file_same_inode(a, b) && a->st_size == b->st_size && S_ISREG(b->st_mode);
}

static inline int file_same_version(const struct stat *a, const struct stat *b) {
#ifdef __APPLE__
    const struct timespec am = a->st_mtimespec, bm = b->st_mtimespec;
    const struct timespec ac = a->st_ctimespec, bc = b->st_ctimespec;
#else
    const struct timespec am = a->st_mtim, bm = b->st_mtim;
    const struct timespec ac = a->st_ctim, bc = b->st_ctim;
#endif
    return file_same_extent(a, b) && am.tv_sec == bm.tv_sec && am.tv_nsec == bm.tv_nsec &&
           ac.tv_sec == bc.tv_sec && ac.tv_nsec == bc.tv_nsec;
}
