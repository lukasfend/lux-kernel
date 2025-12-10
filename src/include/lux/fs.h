/*
 * Date: 2025-12-10 00:00 UTC
 * Author: Lukas Fend <lukas.fend@outlook.com>
 * Description: Simple Unix-like filesystem interface exposed to the shell.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

#define FS_NAME_MAX 32u

struct fs_stat {
    bool is_dir;
    size_t size;
};

struct fs_dirent {
    char name[FS_NAME_MAX];
    bool is_dir;
    size_t size;
};

typedef void (*fs_dir_iter_cb)(const struct fs_dirent *entry, void *user_data);

bool fs_mount(void);
bool fs_ready(void);

bool fs_touch(const char *path);
bool fs_list(const char *path, fs_dir_iter_cb cb, void *user_data);
bool fs_stat_path(const char *path, struct fs_stat *out_stats);
bool fs_read(const char *path, size_t offset, void *buffer, size_t length, size_t *bytes_read);
bool fs_write(const char *path, size_t offset, const void *buffer, size_t length, bool truncate);
