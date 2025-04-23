/* Kevin Zhang 11354912 zbk618 
 * Emily Hartz-Kuzmicz 11350337 job346 */

#include <linux/block_send.h>
#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/kernel.h>
#include <linux/syscalls.h>
#include <linux/pid.h>

long int pSend(pid_t to, void *sData, unsigned int slen, void *rData, 
        unsigned int *rlen) {
    struct task_struct *to_task;
    struct task_struct *from_task;
    struct msg *smsg;
    struct msg *reply;
    struct msg *new_msg;
    pid_t from_pid;

    to_task = get_pid_task(find_get_pid(to), PIDTYPE_PID);
    from_pid = task_pid_nr(current);
    from_task = get_pid_task(find_get_pid(from_pid), PIDTYPE_PID);

    if (!to_task || !from_task) {
        printk("pSend(): failed to obtain to_task/from_task pid: %d / %d\n", 
                to, from_pid);
        return -ESRCH;
    }

    /* disable interrupts */
    local_irq_disable();

    new_msg = kmalloc(sizeof(struct msg), GFP_KERNEL);
    if (!new_msg) {
        printk("pSend(): failed to kmalloc msg struct\n");
        local_irq_enable();
        return -ENOMEM;
    }

    new_msg->data = kmalloc(slen, GFP_KERNEL);
    if (!new_msg->data) {
        printk("pSend(): failed to kmalloc msg->data\n");
        kfree(new_msg);
        local_irq_enable();
        return -ENOMEM;
    }

    if (copy_from_user(new_msg->data, sData, slen)) {
        printk("pSend(): failed to copy msg from user space\n");
        kfree(new_msg->data);
        kfree(new_msg);
        local_irq_enable();
        return -EFAULT;
    }

    new_msg->len = slen;
    new_msg->pid = from_pid;
    new_msg->next = NULL;

    if (!to_task->queue_head) {
        to_task->queue_head = new_msg;
        wake_up_process(to_task);
    } else {
        smsg = to_task->queue_head;
        while (smsg->next) {
            smsg = smsg->next;
        }
        smsg->next = new_msg;
    }


    from_task->waiting_on_reply = true;
    
    local_irq_enable();

    /* wait for response */
    __set_current_state(TASK_INTERRUPTIBLE);
    schedule();

    /* process reply */
    local_irq_disable();
    reply = from_task->reply;
    if (!reply) {
        printk("pSend(): no reply... this should be unreachable\n");
        local_irq_enable();
        return -EFAULT;
    }

    if ((copy_to_user(rData, reply->data, reply->len)) != 0 || 
        (copy_to_user(rlen, &reply->len, sizeof(int)) != 0)) {
        printk("pSend(): failed to copy reply to user\n");
        local_irq_enable();
        return -EFAULT;
    }
    local_irq_enable();
    return 0;
}

long int pReceive(pid_t *from, void *Data, unsigned int *len) {
    struct task_struct *r_task;
    struct msg *received_msg;
    pid_t r_pid;

    r_pid = task_pid_nr(current);
    r_task = get_pid_task(find_get_pid(r_pid), PIDTYPE_PID);

    if (!r_task) {
        return -ESRCH;
        printk("pReceive(): failed to obtain r_task pid: %d\n", r_pid);
    }

    /* if there are no messages, we wait for one */
    if (r_task->queue_head == NULL) {
        __set_current_state(TASK_INTERRUPTIBLE);
        schedule();
    }

    /* process the message */
    local_irq_disable();
    received_msg = r_task->queue_head;
    r_task->queue_head = received_msg->next;

    if ((copy_to_user(Data, received_msg->data, received_msg->len)) != 0 ||
            (copy_to_user(len, &received_msg->len, sizeof(int))) != 0 ||
            (copy_to_user(from, &received_msg->pid, sizeof(pid_t))) != 0) {
        printk("pReceive(): failed to copy received msg to user space\n");
        local_irq_enable();
        kfree(received_msg->data);
        kfree(received_msg);
        return -EFAULT;
    }
    kfree(received_msg->data);
    kfree(received_msg);
    
    local_irq_enable();
    return 0;
}

long int pReply(pid_t to, void *Data, unsigned int len) {
    struct task_struct *to_task;
    struct task_struct *from_task;
    struct msg *reply_msg;
    pid_t from_pid;

    to_task = get_pid_task(find_get_pid(to), PIDTYPE_PID);
    from_pid = task_pid_nr(current);
    from_task = get_pid_task(find_get_pid(from_pid), PIDTYPE_PID);

    if (!to_task || !from_task) {
        printk("pReply(): failed to obtain to_task/from_task %d / %d\n",
                to, from_pid);
        return -ESRCH;
    }
    
    reply_msg = kmalloc(sizeof(struct msg), GFP_KERNEL);
    if (!reply_msg) {
        printk("pReceive(): failed to malloc msg struct in reply\n");
        return -ENOMEM;
    }

    reply_msg->data = kmalloc(len, GFP_KERNEL);
    if (!reply_msg->data) {
        printk("pReceive(): failed to malloc msg->data in reply\n");
        kfree(reply_msg);
        return -ENOMEM;
    }

    if (copy_from_user(reply_msg->data, Data, len) != 0) {
        printk("pReceive(): failed to copy reply from user\n");
        kfree(reply_msg->data);
        kfree(reply_msg);
        return -EFAULT;
    }

    reply_msg->len = len;
    reply_msg->pid = from_pid;
    reply_msg->next = NULL;
    
    local_irq_disable();

    if (to_task->waiting_on_reply) {
        to_task->reply = reply_msg;
        wake_up_process(to_task);
        local_irq_enable();
    } else {
        printk("pReceive(): to_task is not waiting on reply; unreachable\n");
        kfree(reply_msg->data);
        kfree(reply_msg);
        local_irq_enable();
        return -EFAULT;
    }
    return 0;
}

long int pMsgWaits(void) {
    struct task_struct *from_task;
    struct msg *curr;
    pid_t from_pid;
    int count;

    from_pid = task_pid_nr(current);
    from_task = get_pid_task(find_get_pid(from_pid), PIDTYPE_PID);
    count = 0;
    
    if (!from_task) {
        return -ESRCH;
        printk("pMsgWaits(): failed to obtain from_task\n");
    }

    local_irq_disable();

    curr = from_task->queue_head;
    while (curr) {
        count++;
        curr = curr->next;
    }

    local_irq_enable();
    return count;

}

