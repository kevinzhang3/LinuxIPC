/* Kevin Zhang 11354912 zbk618 
 * Emily Hartz-Kuzmicz 11350337 job346 */

#ifndef BLOCK_SEND_H
#define BLOCK_SEND_H

#include <linux/mutex.h>
#include <linux/types.h>

struct msg {
    char *data;
    int len;
    pid_t pid;
    struct msg *next;
};

/* function prototypes */
long int pSend(pid_t, void *, unsigned int, void *, unsigned int *);
long int pReceive(pid_t *, void *, unsigned int *);
long int pReply(pid_t, void *, unsigned int);
long int pMsgWaits(void);

#endif /* end of BLOCK_SEND_H */
