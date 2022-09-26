#ifndef __BAREMETAL_STRING_H__
#define __BAREMETAL_STRING_H__

static inline void* memcpy( void* dest, const void* src, unsigned long count )
{
    return __builtin_memcpy( dest, src, count );
}

void* memset( void* dest, int ch, unsigned long count )
{
    return __builtin_memset( dest, ch, count );
}

int memcmp(const void * dest, const void * src, unsigned long count)
{
    return __builtin_memcmp( dest, src, count );
}

#endif /* !__BAREMETAL_STRING_H__ */
