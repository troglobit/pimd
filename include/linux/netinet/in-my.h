/*
 * Dummy header file to include the appropriate in.h for Linux
 * The situation is pretty messy, and no guarantee it will work.
 * Use your skills and imagination at your own risk :)
 *
 * Thanks to Jonathan Day for the problem report and the solution
 *
 */
/*
 *  Questions concerning this software should be directed to
 *  Pavlin Ivanov Radoslavov (pavlin@catarina.usc.edu)
 *
 *  $Id: in.h,v 1.8 2000/03/08 09:12:45 pavlin Exp $
 */

#include <features.h>

#if (defined(__GLIBC__) && (defined(__GLIBC_MINOR__)))
# if (__GLIBC__ == 2) && (__GLIBC_MINOR__ == 0)
#  include "in-glibc-2.0.h"
# elif (__GLIBC__ == 2) && (__GLIBC_MINOR__ == 1)
#  include "in-glibc-2.1.h"
# else
#  include <stdint.h>
#  include <netinet/in.h>
# endif /* __GLIBC__ */
#else
# include <linux/types.h>
# include <arpa/inet.h>
#endif

