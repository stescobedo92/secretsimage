// Single translation unit that instantiates the vendored stb image codecs.
// Everything else includes only our own image.hpp.
#define STB_IMAGE_IMPLEMENTATION
#define STBI_WINDOWS_UTF8
#include "stb_image.h"  // NOLINT

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"  // NOLINT
