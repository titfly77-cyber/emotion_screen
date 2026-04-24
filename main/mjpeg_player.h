#pragma once

#include <stdint.h>
#include <stdbool.h>

#define MJPEG_VIDEO_FPS         12
#define MJPEG_BUFFER_SIZE       (240 * 240 * 4 / 10)

bool mjpeg_player_init(void);
void mjpeg_player_deinit(void);
void mjpeg_player_start(void);
void mjpeg_player_stop(void);
bool mjpeg_player_play_frame(void);
bool mjpeg_player_is_ready(void);
bool mjpeg_player_is_playing(void);
void mjpeg_player_switch_video(void);