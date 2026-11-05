#ifndef ASAN_TORRENT_H
#define ASAN_TORRENT_H
#ifdef __cplusplus
extern "C" {
#endif
/* Call serially on an application-owned worker queue. Strings are UTF-8 JSON.
 * Returned strings must be freed with asan_torrent_free_string. No exceptions
 * cross this boundary. The caller owns root_path and its UUID subdirectories.
 * This engine downloads only the explicitly selected file. No HTTP server. */
void *asan_torrent_create(const char *root_path);
char *asan_torrent_add(void *engine, const char *uuid, const char *magnet);
char *asan_torrent_snapshot(void *engine, const char *uuid);
char *asan_torrent_command(void *engine, const char *uuid, const char *command, int file_index);
void asan_torrent_free_string(char *value);
void asan_torrent_destroy(void *engine);
#ifdef __cplusplus
}
#endif
#endif
