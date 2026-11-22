#ifndef _ENDIAN_H
#define _ENDIAN_H
#include <byteswap.h>

#define __LITTLE_ENDIAN __ORDER_LITTLE_ENDIAN__
#define __BIG_ENDIAN    __ORDER_BIG_ENDIAN__
#define __BYTE_ORDER    __BYTE_ORDER__
#define LITTLE_ENDIAN   __LITTLE_ENDIAN
#define BIG_ENDIAN      __BIG_ENDIAN
#define BYTE_ORDER      __BYTE_ORDER

#if __BYTE_ORDER == __LITTLE_ENDIAN
#define htobe16(value) bswap_16(value)
#define htobe32(value) bswap_32(value)
#define htobe64(value) bswap_64(value)
#define htole16(value) ((uint16_t) (value))
#define htole32(value) ((uint32_t) (value))
#define htole64(value) ((uint64_t) (value))
#elif __BYTE_ORDER == __BIG_ENDIAN
#define htobe16(value) ((uint16_t) (value))
#define htobe32(value) ((uint32_t) (value))
#define htobe64(value) ((uint64_t) (value))
#define htole16(value) bswap_16(value)
#define htole32(value) bswap_32(value)
#define htole64(value) bswap_64(value)
#else
#error Unsupported byte order
#endif

#define be16toh(value) htobe16(value)
#define be32toh(value) htobe32(value)
#define be64toh(value) htobe64(value)
#define le16toh(value) htole16(value)
#define le32toh(value) htole32(value)
#define le64toh(value) htole64(value)
#endif
