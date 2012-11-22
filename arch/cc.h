#ifndef _CC_H
#define _CC_H

#include <stdint.h>

typedef uint8_t		u8_t;
typedef int8_t		s8_t;
typedef uint16_t	u16_t;
typedef int16_t		s16_t;
typedef uint32_t	u32_t;
typedef int32_t		s32_t;

typedef uint32_t	mem_ptr_t;

#define X8_F	"02x"
#define U16_F	"u"
#define S16_F	"d"
#define X16_F	"x"
#define U32_F	"u"
#define S32_F	"d"
#define X32_F	"x"
#define SZT_F	"u"

#define PACK_STRUCT_FIELD(x) x
#define PACK_STRUCT_STRUCT __attribute__((packed))
#define PACK_STRUCT_BEGIN
#define PACK_STRUCT_END

#define BYTE_ORDER	LITTLE_ENDIAN

#ifndef LWIP_PLATFORM_DIAG
#define LWIP_PLATFORM_DIAG(msg) do {UARTprintf msg;} while(0)
#endif

#ifndef LWIP_PLATFORM_ASSERT
#define LWIP_PLATFORM_ASSERT(msg)
#endif

#endif
