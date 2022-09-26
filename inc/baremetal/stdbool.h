#ifndef __BAREMETAL_STDBOOL_H__
#define __BAREMETAL_STDBOOL_H__

#ifndef __cplusplus

#if defined __STDC_VERSION__ && __STDC_VERSION__ >= 201710L

#else
#define bool _Bool
#define true 1
#define false 0
#endif

#else /* __cplusplus */

#define _Bool bool

#endif /* !__cplusplus */

#define __bool_true_false_are_defined 1

#endif /* !__BAREMETAL_STDBOOL_H__ */
