/* Created by Jacob Hodgkins.
 * CipherGBA Core 1.0 -- standalone emulator interface.
 * Stable C linkage. Functions return 1 for success unless otherwise documented.
 * The caller owns the handle and must not access it simultaneously from multiple threads.
 * The framebuffer pointer remains owned by the core and is valid until the next
 * frame/reset/ROM load or handle destruction. Pixel format: 0x00RRGGBB.
 */
#ifndef CIPHERGBA_CORE_H
#define CIPHERGBA_CORE_H
#include <stdint.h>
#include <stddef.h>
#if defined(_WIN32)
# if defined(CIPHERGBA_CORE_BUILD)
#  define CIPHERGBA_API __declspec(dllexport)
# elif defined(CIPHERGBA_CORE_STATIC)
#  define CIPHERGBA_API
# else
#  define CIPHERGBA_API __declspec(dllimport)
# endif
#elif defined(__GNUC__) || defined(__clang__)
# define CIPHERGBA_API __attribute__((visibility("default")))
#else
# define CIPHERGBA_API
#endif
#ifdef __cplusplus
extern "C" {
#endif
#define CIPHERGBA_CORE_API_VERSION 1
#define CIPHERGBA_SCREEN_WIDTH 240
#define CIPHERGBA_SCREEN_HEIGHT 160
/* Buttons: A, B, Select, Start, Right, Left, Up, Down, R, L */
enum CIPHERGBA_Button {
    CIPHERGBA_BTN_A = 1u << 0, CIPHERGBA_BTN_B = 1u << 1,
    CIPHERGBA_BTN_SELECT = 1u << 2, CIPHERGBA_BTN_START = 1u << 3,
    CIPHERGBA_BTN_RIGHT = 1u << 4, CIPHERGBA_BTN_LEFT = 1u << 5,
    CIPHERGBA_BTN_UP = 1u << 6, CIPHERGBA_BTN_DOWN = 1u << 7,
    CIPHERGBA_BTN_R = 1u << 8, CIPHERGBA_BTN_L = 1u << 9
};
typedef struct CIPHERGBA_Handle CIPHERGBA_Handle;
CIPHERGBA_API int ciphergba_core_api_version(void);
CIPHERGBA_API const char* ciphergba_core_version(void);
CIPHERGBA_API CIPHERGBA_Handle* ciphergba_create(void);
CIPHERGBA_API void ciphergba_destroy(CIPHERGBA_Handle* handle);
/* Error pointer is owned by the handle; valid until next call on the handle. */
CIPHERGBA_API const char* ciphergba_last_error(const CIPHERGBA_Handle* handle);
CIPHERGBA_API int ciphergba_set_save_directory(CIPHERGBA_Handle* handle, const char* path);
CIPHERGBA_API int ciphergba_load_rom(CIPHERGBA_Handle* handle, const char* path);
CIPHERGBA_API int ciphergba_load_bios(CIPHERGBA_Handle* handle, const char* path);
CIPHERGBA_API int ciphergba_import_bios(CIPHERGBA_Handle* handle, const char* input, const char* copy_to);
CIPHERGBA_API int ciphergba_rom_loaded(const CIPHERGBA_Handle* handle);
CIPHERGBA_API int ciphergba_bios_loaded(const CIPHERGBA_Handle* handle);
CIPHERGBA_API const char* ciphergba_rom_title(CIPHERGBA_Handle* handle);
CIPHERGBA_API uint32_t ciphergba_rom_crc32(const CIPHERGBA_Handle* handle);
CIPHERGBA_API void ciphergba_reset(CIPHERGBA_Handle* handle);
/* Read-only bus observations for external diagnostic harnesses. */
CIPHERGBA_API uint8_t ciphergba_read8(const CIPHERGBA_Handle* handle,uint32_t address);
CIPHERGBA_API uint16_t ciphergba_read16(const CIPHERGBA_Handle* handle,uint32_t address);
CIPHERGBA_API void ciphergba_set_buttons(CIPHERGBA_Handle* handle, uint16_t buttons);
/* Returns 1 when a completed video frame was emulated, 0 otherwise. */
CIPHERGBA_API int ciphergba_run_frame(CIPHERGBA_Handle* handle);
CIPHERGBA_API uint64_t ciphergba_frame_count(const CIPHERGBA_Handle* handle);
CIPHERGBA_API const uint32_t* ciphergba_get_framebuffer(const CIPHERGBA_Handle* handle);
/* Save-memory persistence is part of cartridge hardware operation. */
CIPHERGBA_API int ciphergba_flush_save(CIPHERGBA_Handle* handle);
#ifdef __cplusplus
}
#endif
#endif
