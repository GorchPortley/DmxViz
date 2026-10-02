/* Implementations of the single-header C libraries used by DmxViz.
 *
 * They live in their own target (dep_impls) because third-party implementation
 * code does not pass DmxViz's strict warning flags (/W4 /WX, -Wall -Werror).
 * First-party code only includes the headers. */

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_HDR
#define STBI_NO_PIC
#define STBI_NO_PNM
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include <stdio.h>
#include <string.h>
#include <math.h>
#define NANOSVG_IMPLEMENTATION
#include "nanosvg.h"
#define NANOSVGRAST_IMPLEMENTATION
#include "nanosvgrast.h"

#define CGLTF_IMPLEMENTATION
#include "cgltf.h"
