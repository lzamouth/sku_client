/* Nuklear configuration shared by every app source file. main.c defines the
 * implementation; the others only get the declarations. */
#ifndef SKU_NUKLEAR_CONFIG_H
#define SKU_NUKLEAR_CONFIG_H

#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_STANDARD_BOOL
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_INCLUDE_COMMAND_USERDATA
#define NK_INCLUDE_VERTEX_BUFFER_OUTPUT
#define NK_INCLUDE_FONT_BAKING
#include "nuklear.h"
#include "nuklear_sdl3_renderer.h"

#endif
