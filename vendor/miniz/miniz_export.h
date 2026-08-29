#ifndef MINIZ_EXPORT_H
#define MINIZ_EXPORT_H

/* potato-launcher builds miniz as a static library, so all export
 * decorations are empty. This header replaces the one normally generated
 * by CMake's generate_export_header(). */

#ifndef MINIZ_STATIC_DEFINE
#define MINIZ_STATIC_DEFINE
#endif

#define MINIZ_EXPORT
#define MINIZ_NO_EXPORT

#ifndef MINIZ_DEPRECATED
#define MINIZ_DEPRECATED __attribute__((__deprecated__))
#endif

#ifndef MINIZ_DEPRECATED_EXPORT
#define MINIZ_DEPRECATED_EXPORT MINIZ_EXPORT MINIZ_DEPRECATED
#endif

#ifndef MINIZ_DEPRECATED_NO_EXPORT
#define MINIZ_DEPRECATED_NO_EXPORT MINIZ_NO_EXPORT MINIZ_DEPRECATED
#endif

#define MINIZ_DEFINED_ABI

#endif /* MINIZ_EXPORT_H */
