#ifndef __BAREMETAL_STDINT_H__
#define __BAREMETAL_STDINT_H__

#ifdef __INT8_TYPE__
typedef __INT8_TYPE__ int8_t;
#endif
#ifdef __INT16_TYPE__
typedef __INT16_TYPE__ int16_t;
#endif
#ifdef __INT32_TYPE__
typedef __INT32_TYPE__ int32_t;
#endif

#ifndef _USER_TYPES_H_
#ifdef __INT64_TYPE__
typedef __INT64_TYPE__ int64_t;
#endif
#endif

#ifdef __UINT8_TYPE__
typedef __UINT8_TYPE__ uint8_t;
#endif
#ifdef __UINT16_TYPE__
typedef __UINT16_TYPE__ uint16_t;
#endif
#ifdef __UINT32_TYPE__
typedef __UINT32_TYPE__ uint32_t;
#endif

#ifndef _USER_TYPES_H_
#ifdef __UINT64_TYPE__
typedef __UINT64_TYPE__ uint64_t;
#endif
#endif

#endif
