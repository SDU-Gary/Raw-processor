#define TINYEXR_IMPLEMENTATION
#if defined(TINYEXR_USE_MINIZ) && (TINYEXR_USE_MINIZ==0)
#include <zlib.h>
#endif
#include "rawproc/tinyexr.h"
