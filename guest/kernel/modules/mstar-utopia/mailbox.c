// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-only
#include "private.h"

/* ---- /dev/msmailbox (175): MStar's mailbox, with optional CP and PM peers ----
 *
 * MStar's mailbox driver (drivers/mstar2/drv/mbx in LG's GPL kernel of W18R: mdrv_mbx_io.c,
 * mdrv_mbx.c, mdrv_mbx_msgpool.c; the ioctls of include/mdrv_mbx_io.h as a 32-bit process sees
 * them, 'm' 0-16). A process (its file) INITs, ENABLEs reception and REGISTERs message classes,
 * each with a queue; a class has one owner, and a REGISTER by another file takes it over (as
 * MStar's MDrv_MSGQ_RegisterMSG does; here the pending messages stay, where MStar's reset the
 * queue: the TEE's reply class 52 is registered by every process that queries a class, and a
 * reply lost that way leaves its process in KADP's endless receive loop). SENDMSG gives a message
 * to its role's CPU; a message from a CPU goes to its class's queue if the class is registered
 * and its owner enabled (else it is dropped, as the hardware's "receive disabled"), and RECVMSG
 * takes the first one of the type asked for, waiting up to its timeout. INSTANT messages queue
 * apart from NORMAL ones. The owner is signalled (fasync, as MStar's kill_fasync; the band is
 * plain POLL_IN: MStar's kernel added the class << 8 to it, which this kernel's send_sigio
 * rejects; KADP's handler then finds no callback for class 0 and returns).
 *
 * The secure-memory profile supplies the co-processor role (E_MBX_ROLE_CP, 1), as the secure
 * world of MStar's TEE client in KADP (W18R's libkadaptor: TEEC_*, _fill_msg, _handle_Msg,
 * _ca_*_mbx_by_utopia, MApi_MBX_QueryDynamicClass) uses it. The pm_micom profile independently
 * supplies role PM (2), forwarding supported controller reads to the virtual micom below.
 * Unsupported requests fail with E_MBX_ERR_PEER_CPU_NOT_ALIVE, as when a peer never takes
 * the mail. The secure world answers what its client sends:
 * - class 58 (E_MBX_CLASS_QUERY_CLASS) index 1, "which class is the session <name>" (parameter 0
 *   the client's own guess, then the name): the reply, INSTANT on class 52
 *   (E_MBX_CLASS_SECUREBOOT_WAIT), has the class in parameter 0. Each query gets a class of its
 *   own, from 254 down to 60, held until the session is closed: KADP keeps its sessions (class
 *   included) in SysV shared memory, and any process may use one another opened.
 * - on such a class (_fill_msg: index the command, parameters 0-3 the SHM offset, 4-7 the length,
 *   8-9 two session bytes): index 1 (open the session) and 2 (close it) are done, nothing to
 *   return: the reply is the request with parameter 8, the TEE's status, 1 ("done", _handle_Msg).
 *   Any other command is not implemented: status 0 (the TEE failed it, _handle_Msg's error), the
 *   shared memory untouched; it is logged the first time each (session, command) is seen.
 * /proc/mstar-utopia/tee lists what came: session, command, count, answer. */

#define MBX_ROLE_HK         0
#define MBX_ROLE_CP         1
#define MBX_ROLE_PM         2
#define MBX_ROLES           5
#define MBX_CPU_AEON        1
#define MBX_CPU_MIPS        2
#define MBX_MSG_NORMAL      0
#define MBX_MSG_INSTANT     1
#define MBX_CHECK_INSTANT   0x2
#define MBX_CHECK_NORMAL    0x4
#define MBX_CHECK_ALL       (MBX_CHECK_INSTANT | MBX_CHECK_NORMAL)
#define MBX_CHECK_BLOCK     0x80000000
#define MBX_CLASSES         255 /* E_MBX_CLASS_MAX */
#define MBX_QUEUE_MAX       256 /* MAX_MBX_QUEUE_SIZE */
#define MBX_INFO_SIZE       8   /* MAX_MBX_INFORMATION_SIZE */
#define MBX_STATUS_OVERFLOW 0x1
#define MBX_STATUS_INSTANT  0x2
#define MBX_STATUS_NORMAL   0x4

enum mbx_result {
    MBX_SUCCESS = 0,
    MBX_ERR_NOT_INITIALIZED = 1,
    MBX_ERR_SLOT_BUSY = 3,
    MBX_ERR_SLOT_ALREADY_OPENED = 4,
    MBX_ERR_SLOT_NOT_OPENED = 5,
    MBX_ERR_INVALID_CPU_ID = 6,
    MBX_ERR_INVALID_PARAM = 7,
    MBX_ERR_PEER_CPU_NOT_ALIVE = 9,
    MBX_ERR_NO_MORE_MSG = 14,
    MBX_ERR_TIME_OUT = 13,
    MBX_ERR_HAS_MSG_PENDING = 15,
};

/* the secure world's classes (MStar's MBX_Class) */
#define MBX_CLASS_SECURE_WAIT 52
#define MBX_CLASS_QUERY_CLASS 58
#define TEE_CLASS_FIRST       60 /* above E_MBX_CLASS_NUM (59) */
#define TEE_CLASS_LAST        254
#define TEE_QUERY_INDEX       1 /* E_Dynamic_QueryCalsss */
#define TEE_CMD_OPEN          1 /* TEEC_OpenSession's _invoke_command */
#define TEE_CMD_CLOSE         2 /* _closeSession's */
#define TEE_STATUS            8 /* the reply's parameter: 1 done, 0 failed */
#define TEE_NAME_LEN          16

struct mbx_msg { /* MBX_Msg: 24 bytes */
    u32 role, type;
    u8 ctrl, cls, index, count;
    u8 params[10];
    u8 s0, s1;
};
static_assert(sizeof(struct mbx_msg) == 24);

/* the ioctls' arguments from a 32-bit process (pointers are u32, enums 4 bytes) */
struct mbx_init32 {
    u32 hk_cpu, host_role, timeout_ms, result;
};
struct mbx_binfo32 {
    u8 info;
    u8 pad[3];
    u32 result;
};
struct mbx_register32 {
    u32 cls;
    u16 queue_size;
    u16 pad;
    u32 result;
};
struct mbx_unregister32 {
    u32 cls;
    u8 force;
    u8 pad[3];
    u32 result;
};
struct mbx_clear32 {
    u32 cls;
    u32 result;
};
struct mbx_send32 {
    u32 msg;
    u32 result;
};
struct mbx_recv32 {
    u32 cls;
    u32 msg;
    u32 wait_ms;
    u32 flag;
    u32 result;
};
struct mbx_msgq32 {
    u32 cls;
    u32 status;
    u32 result;
};
struct mbx_drvstatus {
    u8 enabled;
    u8 pad[3];
    s32 refcnt;
};
struct mbx_info32 {
    u32 role;
    u32 info;
    u8 size;
    u8 pad[3];
    u32 result;
};
struct mbx_msgq_status {
    u32 status, normal, instant;
};

#define MBX_IOC(nr, type)          _IOC(_IOC_READ | _IOC_WRITE, 'm', nr, sizeof(type))
#define MBX_IOC_INIT               MBX_IOC(0, struct mbx_init32)
#define MBX_IOC_DEINIT             MBX_IOC(1, struct mbx_binfo32)
#define MBX_IOC_REGISTERMSG        MBX_IOC(2, struct mbx_register32)
#define MBX_IOC_UNREGISTERMSG      MBX_IOC(3, struct mbx_unregister32)
#define MBX_IOC_CLEARMSG           MBX_IOC(4, struct mbx_clear32)
#define MBX_IOC_SENDMSG            MBX_IOC(5, struct mbx_send32)
#define MBX_IOC_SENDMSGLOOPBACK    MBX_IOC(6, struct mbx_send32)
#define MBX_IOC_RECVMSG            MBX_IOC(7, struct mbx_recv32)
#define MBX_IOC_GETMSGQSTATUS      MBX_IOC(8, struct mbx_msgq32)
#define MBX_IOC_MBXENABLE          MBX_IOC(9, struct mbx_binfo32)
#define MBX_IOC_SETINFORMATION     MBX_IOC(10, struct mbx_info32)
#define MBX_IOC_GETINFORMATION     MBX_IOC(11, struct mbx_info32)
#define MBX_IOC_GETDRVSTATUS       MBX_IOC(12, struct mbx_drvstatus)
#define MBX_IOC_CHECKMSG           MBX_IOC(13, struct mbx_recv32)
#define MBX_IOC_REMOVEMSG          MBX_IOC(14, struct mbx_binfo32)
#define PM_IOC_GET_BRICKTERMINATOR MBX_IOC(15, u8)
#define PM_IOC_SET_BRICKTERMINATOR MBX_IOC(16, u8)

struct mbx_file {
    bool inited, enabled;
    struct fasync_struct *fasync;
    u32 id; /* the trace's name of this file: 1, 2, ... in open order */
};

struct mbx_queued {
    struct list_head node;
    struct mbx_msg msg;
};

static struct mbx_class {
    struct mbx_file *owner;    /* NULL: not registered */
    u16 queue_size, queued[2]; /* queued[instant] */
    struct list_head normal, instant;
    /* the secure world's: a TEE session's class, and the session's name */
    bool tee_session;
    char tee_name[TEE_NAME_LEN];
} mbx_classes[MBX_CLASSES];

static DEFINE_MUTEX(mbx_lock);            /* the classes, their queues, the files' state */
static DECLARE_WAIT_QUEUE_HEAD(mbx_wait); /* a message arrived */
static int mbx_refcnt;
static u8 mbx_info[MBX_ROLES][MBX_INFO_SIZE]; /* SETINFORMATION's bytes, by target role */
static u8 mbx_brick_terminator;               /* the PM's dummy register */
static struct {
    struct mbx_class *c;
    bool instant;
} mbx_checked; /* CHECKMSG's, for REMOVEMSG */

static struct tee_seen {
    char name[TEE_NAME_LEN];
    char comm[TASK_COMM_LEN]; /* the first caller */
    u8 index, status;
    unsigned long count;
} tee_seen[64];

/* The mailbox trace (debugging; mbx_trace=1 at load, e.g. mstar_utopia.mbx_trace=1 on the kernel
 * cmdline): the last MBX_TRACE_LEN operations on /dev/msmailbox, oldest first, in
 * /proc/mstar-utopia/mailbox. Each line: guest time, tgid/pid and comm of the caller, the
 * operation, the class, the message's index and TEE status (parameter 8) where there is one, its
 * type (0 NORMAL, 1 INSTANT), ctrl byte, parameter count and first parameter, the class's queue
 * after it, the caller's file and the class's owner (trace ids, 0 none), the result,
 * and aux: the messages discarded by a clear/unregister/deinit/release, the previous owner of a
 * takeover, the target role of a send, the wait's outcome of a recv (jiffies left, 0 timed out,
 * -512 interrupted), the flag of a check. /dev/semutex's contended mutexes go into it too:
 * sem-wait (a lock that has to wait; holder, and aux how long it has held it, ms), sem-lock (that
 * lock taken; aux the wait, ms, at least 1), sem-timeout (a LOCK_WITHTIMEOUT that gave up; aux its
 * limit), sem-unlock (an unlock somebody waits for; aux how long it was held), sem-release (a
 * file released holding it), each with the mutex's index and name. */
static bool mbx_trace;
static bool pm_micom;
module_param(pm_micom, bool, 0444);
MODULE_PARM_DESC(pm_micom, "PM51 micom mailbox transport (W21A)");
bool mailbox_enabled(void)
{
    return memory_has_tee() || pm_micom;
}
module_param(mbx_trace, bool, 0444);
MODULE_PARM_DESC(mbx_trace, "trace /dev/msmailbox's operations to /proc/mstar-utopia/mailbox "
                            "(debugging; default off)");

#define MBX_TRACE_LEN 8192

enum mbx_op {
    MBX_OP_OPEN,
    MBX_OP_RELEASE,
    MBX_OP_INIT,
    MBX_OP_DEINIT,
    MBX_OP_ENABLE,
    MBX_OP_REGISTER,
    MBX_OP_TAKEOVER,
    MBX_OP_UNREGISTER,
    MBX_OP_CLEAR,
    MBX_OP_SEND,
    MBX_OP_ARRIVE,
    MBX_OP_DROP,
    MBX_OP_RECV,
    MBX_OP_CHECK,
    MBX_OP_REMOVE,
};

static const char *const mbx_op_names[] = {
    "open",  "release", "init",   "deinit", "enable", "register", "takeover", "unregister",
    "clear", "send",    "arrive", "drop",   "recv",   "check",    "remove",
};

static struct mbx_ev {
    u64 ns;
    pid_t tgid, pid;
    char comm[TASK_COMM_LEN];
    u8 op, cls, index, status, type, ctrl, count, p0;
    u16 queued;
    u32 file, owner, result;
    s32 aux;
    pid_t htgid, hpid; /* semutex: the holder (the waiter's at a wait) */
    char what[24];     /* semutex: the mutex's name */
} *mbx_evs;
static unsigned long mbx_ev_next; /* the next slot, counted from the start */
static DEFINE_SPINLOCK(mbx_ev_lock);
static atomic_t mbx_file_ids = ATOMIC_INIT(0);

/* one trace line; c the class (NULL: none), msg the message (NULL: none) */
static void mbx_ev(u8 op, struct mbx_class *c, const struct mbx_file *f, const struct mbx_msg *msg,
                   u32 result, s32 aux)
{
    struct mbx_ev *e;
    unsigned long flags;

    if (!mbx_evs)
        return;
    spin_lock_irqsave(&mbx_ev_lock, flags);
    e = &mbx_evs[mbx_ev_next++ % MBX_TRACE_LEN];
    *e = (struct mbx_ev){
        .ns = ktime_get_boottime_ns(),
        .tgid = current->tgid,
        .pid = current->pid,
        .op = op,
        .cls = c     ? c - mbx_classes
               : msg ? msg->cls
                     : 0,
        .index = msg ? msg->index : 0,
        .status = msg ? msg->params[TEE_STATUS] : 0,
        .type = msg ? msg->type : 0,
        .ctrl = msg ? msg->ctrl : 0,
        .count = msg ? msg->count : 0,
        .p0 = msg ? msg->params[0] : 0,
        .queued = c ? READ_ONCE(c->queued[0]) + READ_ONCE(c->queued[1]) : 0,
        .file = f ? f->id : 0,
        .owner = c && c->owner ? c->owner->id : 0,
        .result = result,
        .aux = aux,
    };
    get_task_comm(e->comm, current);
    spin_unlock_irqrestore(&mbx_ev_lock, flags);
}

/* a semutex line: mutex i (its index less 0xff), under semutex_lock */
void mailbox_sem_event(u8 op, u32 index, int result, s32 aux, pid_t tgid, pid_t pid,
                       const char *name)
{
    struct mbx_ev *e;
    unsigned long flags;

    if (!mbx_evs)
        return;
    spin_lock_irqsave(&mbx_ev_lock, flags);
    e = &mbx_evs[mbx_ev_next++ % MBX_TRACE_LEN];
    *e = (struct mbx_ev){
        .ns = ktime_get_boottime_ns(),
        .tgid = current->tgid,
        .pid = current->pid,
        .op = op,
        .file = index,
        .result = result,
        .aux = aux,
        .htgid = tgid,
        .hpid = pid,
    };
    strscpy(e->what, name, sizeof(e->what));
    get_task_comm(e->comm, current);
    spin_unlock_irqrestore(&mbx_ev_lock, flags);
}

static void *mbx_trace_start(struct seq_file *m, loff_t *pos)
{
    unsigned long next = READ_ONCE(mbx_ev_next),
                  first = next > MBX_TRACE_LEN ? next - MBX_TRACE_LEN : 0;

    return first + *pos < next ? (void *)(uintptr_t)(first + *pos + 1) : NULL;
}

static void *mbx_trace_next(struct seq_file *m, void *v, loff_t *pos)
{
    ++*pos;
    return mbx_trace_start(m, pos);
}

static void mbx_trace_stop(struct seq_file *m, void *v)
{
}

static int mbx_trace_show(struct seq_file *m, void *v)
{
    struct mbx_ev e;
    unsigned long flags;

    spin_lock_irqsave(&mbx_ev_lock, flags);
    e = mbx_evs[((uintptr_t)v - 1) % MBX_TRACE_LEN];
    spin_unlock_irqrestore(&mbx_ev_lock, flags);
    if (e.op >= SEM_OP_WAIT) {
        static const char *const names[] = {"sem-wait",    "sem-lock",   "sem-try",
                                            "sem-timeout", "sem-unlock", "sem-release"};

        seq_printf(m, "%5llu.%06llu %5d/%-5d %-15s %-10s %u %s holder %d/%d res %d aux %d\n",
                   e.ns / NSEC_PER_SEC, e.ns % NSEC_PER_SEC / NSEC_PER_USEC, e.tgid, e.pid, e.comm,
                   names[e.op - SEM_OP_WAIT], e.file, e.what, e.htgid, e.hpid, (int)e.result,
                   e.aux);
        return 0;
    }
    seq_printf(m,
               "%5llu.%06llu %5d/%-5d %-15s %-10s cls %3u idx %3u st %u t %u ctrl %u n %2u p0 %3u "
               "q %3u file %u owner %u res %u aux %d\n",
               e.ns / NSEC_PER_SEC, e.ns % NSEC_PER_SEC / NSEC_PER_USEC, e.tgid, e.pid, e.comm,
               mbx_op_names[e.op], e.cls, e.index, e.status, e.type, e.ctrl, e.count, e.p0,
               e.queued, e.file, e.owner, e.result, e.aux);
    return 0;
}

static const struct seq_operations mbx_trace_seq_ops = {
    .start = mbx_trace_start,
    .next = mbx_trace_next,
    .stop = mbx_trace_stop,
    .show = mbx_trace_show,
};

/* the pending messages of a class go; returns how many */
static int mbx_class_clear(struct mbx_class *c)
{
    struct mbx_queued *q, *n;
    int count = c->queued[0] + c->queued[1];

    list_for_each_entry_safe(q, n, &c->normal, node) kfree(q);
    list_for_each_entry_safe(q, n, &c->instant, node) kfree(q);
    INIT_LIST_HEAD(&c->normal);
    INIT_LIST_HEAD(&c->instant);
    WRITE_ONCE(c->queued[0], 0);
    WRITE_ONCE(c->queued[1], 0);
    if (mbx_checked.c == c)
        mbx_checked.c = NULL;
    return count;
}

/* A message from a CPU arrives (MStar's _MDrv_MBX_MsgRecvCb, MDrv_MSGQ_AddMSG). Under mbx_lock. */
static void mbx_arrive(const struct mbx_msg *msg)
{
    struct mbx_class *c = &mbx_classes[msg->cls % MBX_CLASSES];
    struct mbx_queued *q;

    if (msg->cls >= MBX_CLASSES || !c->owner || !c->owner->enabled) {
        pr_info_ratelimited("msmailbox: class %u not registered or not enabled: message dropped\n",
                            msg->cls);
        mbx_ev(MBX_OP_DROP, msg->cls < MBX_CLASSES ? c : NULL, NULL, msg, MBX_ERR_NOT_INITIALIZED,
               0);
        return;
    }
    if (c->queued[0] + c->queued[1] >= c->queue_size) {
        pr_info_ratelimited("msmailbox: class %u overflows: message dropped\n", msg->cls);
        mbx_ev(MBX_OP_DROP, c, NULL, msg, MBX_STATUS_OVERFLOW, 0);
        return;
    }
    q = kmalloc(sizeof(*q), GFP_KERNEL);
    if (!q)
        return;
    q->msg = *msg;
    list_add_tail(&q->node, msg->type == MBX_MSG_INSTANT ? &c->instant : &c->normal);
    WRITE_ONCE(c->queued[msg->type == MBX_MSG_INSTANT],
               c->queued[msg->type == MBX_MSG_INSTANT] + 1);
    mbx_ev(MBX_OP_ARRIVE, c, NULL, msg, MBX_SUCCESS, 0);
    kill_fasync(&c->owner->fasync, SIGIO, POLL_IN);
    wake_up_interruptible_all(&mbx_wait);
}

static void tee_count(const char *name, u8 index, u8 status)
{
    struct tee_seen *e = NULL;

    for (size_t i = 0; i < ARRAY_SIZE(tee_seen); i++) {
        if (!tee_seen[i].count || (tee_seen[i].index == index && !strcmp(tee_seen[i].name, name))) {
            e = &tee_seen[i];
            break;
        }
    }
    if (!e)
        return;
    if (!e->count++) {
        strscpy(e->name, name, sizeof(e->name));
        get_task_comm(e->comm, current);
        e->index = index;
        e->status = status;
        if (!status)
            pr_info("tee: session \"%s\" command %u (from %s) not implemented: failed\n", name,
                    index, e->comm);
    }
}

/* The secure world takes a message sent to the co-processor (see above). Under mbx_lock. */
static void tee_take(const struct mbx_msg *msg)
{
    struct mbx_class *c = &mbx_classes[msg->cls];
    struct mbx_msg reply = {.role = MBX_ROLE_CP, .type = MBX_MSG_INSTANT, .index = msg->index};
    static DECLARE_BITMAP(unknown, MBX_CLASSES);

    if (msg->cls == MBX_CLASS_QUERY_CLASS && msg->index == TEE_QUERY_INDEX) {
        char name[TEE_NAME_LEN] = "";
        int cls;

        memcpy(name, msg->params + 1, min_t(size_t, sizeof(msg->params) - 1, sizeof(name) - 1));
        for (cls = TEE_CLASS_LAST; cls >= TEE_CLASS_FIRST; cls--)
            if (!mbx_classes[cls].tee_session && !mbx_classes[cls].owner)
                break;
        if (cls >= TEE_CLASS_FIRST) {
            mbx_classes[cls].tee_session = true;
            strscpy(mbx_classes[cls].tee_name, name, TEE_NAME_LEN);
        } else {
            cls = 0; /* none left: the client's query fails */
        }
        tee_count(name, 0, cls != 0);
        reply.cls = MBX_CLASS_SECURE_WAIT;
        reply.count = 1;
        reply.params[0] = cls;
        mbx_arrive(&reply);
        return;
    }
    if (!c->tee_session) {
        if (!test_and_set_bit(msg->cls, unknown))
            pr_info("tee: class %u index %u: no session there, no answer\n", msg->cls, msg->index);
        return;
    }
    reply = *msg;
    reply.role = MBX_ROLE_CP;
    reply.type = MBX_MSG_INSTANT;
    reply.params[TEE_STATUS] = msg->index == TEE_CMD_OPEN || msg->index == TEE_CMD_CLOSE;
    tee_count(c->tee_name, msg->index, reply.params[TEE_STATUS]);
    mbx_arrive(&reply);
    if (msg->index == TEE_CMD_CLOSE)
        c->tee_session = false; /* the session is gone: its class can be given again */
}

/* The PM's micom commands, class 48 (W21A's libhal_ucom and libkadaptor).
 * HAL_UCOM_ReadCommand(cmd, len) sends index = cmd and one parameter, the length it wants, and
 * _MAdp_MBX_HK2PM_ReceiveMsg then takes a NORMAL reply on class 48 with the bytes.
 * HAL_UCOM_WriteCommand(buf, len) sends index = buf[0], count = len - 1 and the parameters
 * buf[1..], and takes no reply. Both go through MAdp_MBX_HK2PM_SendRcv, whose flags (3 for a
 * read, 1 for a write: send, receive) stay in the caller: _MAdp_MBX_HK2PM_SendMsg builds the same
 * message for both, role 2, type 1 (INSTANT), ctrl 0, s0/s1 0. A write with one parameter is
 * therefore a read's message, and the command decides, as it does in the PM's own firmware.
 *
 * pm_writes is the set of buf[0] of every HAL_UCOM_WriteCommand in W21A 03.53.25.01: its only
 * caller is libmicom_cmnio's micom_write, called by 118 sites in libmicom_timer, _misc, _sensor,
 * _led, _cec and _power, each with a constant command byte (no other library in the rootfs or
 * bsppart sends to class 48). Two of them are read as well, with a length of 1: 0x70
 * (Set_ToolType, Get_EyeSensorStatus) and 0xf5 (Set_SpecialRemoconModeOnOff,
 * Get_PowerIndicatorInfo); that one message stays a read, which is not served: with the mailbox
 * trace, neither command was sent at all during boot, Quick Start+'s standby and wake, a reboot
 * or a power-off (2026-10-29). The table is sorted. Reads serve the virtual micom's 0x9a (power-on
 * mode), 0xb1 (power state), 0xc0 (the three-byte event poll) and the battery's 0x09, 0x0b, 0x0c,
 * 0x0d and 0x0f (N21D's libmicom_sensor, which the micom answers only on a set with a battery,
 * lgemu i2c.c battery_read); other reads keep the peer failure, not fabricated data. Writes go to
 * the virtual micom as the I2C write buf[0..len-1] the other SoCs' micoms get (lgemu i2c.c
 * micom_write: 25 00 power off, 44 restart, 27/52 the power-off timer, 25 02 and 26 07 Quick
 * Start+'s standby). */
static const u8 pm_writes[] = {
    0x01, 0x02, 0x03, 0x04, 0x06, 0x08, 0x0a, 0x0e, 0x10, 0x12, 0x14, 0x15, 0x1a, 0x1b, 0x1c, 0x20,
    0x21, 0x23, 0x24, 0x25, 0x26, 0x27, 0x2a, 0x2d, 0x2e, 0x30, 0x31, 0x32, 0x36, 0x37, 0x38, 0x39,
    0x3a, 0x3b, 0x3c, 0x3d, 0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x4b,
    0x4c, 0x4d, 0x4e, 0x4f, 0x50, 0x52, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5a, 0x5b, 0x5c, 0x5e, 0x5f,
    0x60, 0x64, 0x69, 0x6a, 0x6b, 0x6c, 0x6d, 0x6e, 0x70, 0x72, 0x73, 0x75, 0x77, 0x78, 0x7b, 0x7c,
    0x7f, 0x81, 0x8c, 0x92, 0x94, 0x96, 0x98, 0x99, 0xc9, 0xca, 0xcb, 0xcd, 0xce, 0xd2, 0xd4, 0xd5,
    0xd7, 0xdd, 0xdf, 0xe4, 0xe9, 0xea, 0xec, 0xee, 0xf1, 0xf2, 0xf4, 0xf5, 0xf7, 0xfa, 0xfb, 0xfe};

static const u8 pm_reads[] = {0x09, 0x0b, 0x0c, 0x0d, 0x0f, 0x9a, 0xb1, 0xc0};

/* whether a class 48 message is a micom write (see above) */
static bool pm_is_write(const struct mbx_msg *msg)
{
    if (!memchr(pm_writes, msg->index, sizeof(pm_writes)))
        return false;
    if ((msg->index == 0x70 || msg->index == 0xf5) && msg->count == 1 && msg->params[0] == 1)
        return false;
    return true;
}

static u32 pm_write(const struct mbx_msg *msg)
{
    u8 buf[1 + sizeof(msg->params)];
    struct i2c_msg w = {.len = 1 + msg->count, .buf = buf};

    if (msg->count > sizeof(msg->params))
        return MBX_ERR_INVALID_PARAM;
    buf[0] = msg->index;
    memcpy(buf + 1, msg->params, msg->count);
    if (lgemu_i2c_xfer(LGEMU_I2C_MICOM, &w, 1) < 0)
        return MBX_ERR_PEER_CPU_NOT_ALIVE;
    return MBX_SUCCESS;
}

static u32 pm_take(const struct mbx_msg *msg)
{
    struct mbx_msg reply = {
        .role = MBX_ROLE_PM, .type = MBX_MSG_NORMAL, .cls = 48, .index = msg->index};
    u8 command = msg->index;
    struct i2c_msg msgs[] = {
        {.len = 1, .buf = &command},
        {.flags = I2C_M_RD, .buf = reply.params},
    };

    if (msg->cls != 48)
        return MBX_ERR_INVALID_PARAM;
    if (pm_is_write(msg))
        return pm_write(msg);
    if (msg->count != 1 || !msg->params[0] || msg->params[0] > 10)
        return MBX_ERR_INVALID_PARAM;
    if (!memchr(pm_reads, command, sizeof(pm_reads)))
        return MBX_ERR_PEER_CPU_NOT_ALIVE;
    reply.count = msgs[1].len = msg->params[0];
    if (lgemu_i2c_xfer(LGEMU_I2C_MICOM, msgs, ARRAY_SIZE(msgs)) < 0)
        return MBX_ERR_PEER_CPU_NOT_ALIVE;
    mbx_arrive(&reply);
    return MBX_SUCCESS;
}

/* SENDMSG (MStar's _MDrv_MBX_SendMsg): the message goes to its role's CPU. Under mbx_lock. */
static u32 mbx_send(struct mbx_msg *msg)
{
    static DECLARE_BITMAP(dead, MBX_ROLES * MBX_CLASSES);

    if (msg->cls >= MBX_CLASSES || msg->role >= MBX_ROLES)
        return MBX_ERR_INVALID_PARAM;
    if (msg->role == MBX_ROLE_CP && memory_has_tee()) {
        tee_take(msg);
        return MBX_SUCCESS;
    }
    if (msg->role == MBX_ROLE_PM && pm_micom) {
        u32 ret = pm_take(msg);
        if (ret != MBX_ERR_PEER_CPU_NOT_ALIVE)
            return ret;
    }
    if (!test_and_set_bit(msg->role * MBX_CLASSES + msg->cls, dead))
        pr_info("msmailbox: role %u class %u index %u: no such CPU, not alive\n", msg->role,
                msg->cls, msg->index);
    if (msg->role == MBX_ROLE_PM && pm_micom) {
        static DECLARE_BITMAP(seen, 256);
        if (!test_and_set_bit(msg->index, seen))
            pr_info("PM request class %u index %02x count %u: %*ph\n", msg->cls, msg->index,
                    msg->count, min_t(int, msg->count, 10), msg->params);
    }
    return MBX_ERR_PEER_CPU_NOT_ALIVE;
}

/* Utopia 700's mdrv_mbx_st.h packs the enum/result after its byte or u16
 * field. Keep the older naturally aligned ioctl layouts as well. */
static unsigned int mbx_aligned_cmd(unsigned int cmd)
{
    unsigned int nr = _IOC_NR(cmd), size = _IOC_SIZE(cmd), aligned = 0;

    if (size == 5 && (nr == 1 || nr == 9 || nr == 12 || nr == 14))
        aligned = 8;
    if (size == 10 && nr == 2)
        aligned = 12;
    if (size == 9 && nr == 3)
        aligned = 12;
    if (size == 13 && (nr == 10 || nr == 11))
        aligned = 16;
    return aligned ? _IOC(_IOC_DIR(cmd), 'm', nr, aligned) : cmd;
}

/* the first queued message of the flag's types in a class, NULL if none. Under mbx_lock. */
static struct mbx_queued *mbx_first(struct mbx_class *c, u32 flag, bool *instant)
{
    if ((flag & MBX_CHECK_INSTANT) && !list_empty(&c->instant)) {
        *instant = true;
        return list_first_entry(&c->instant, struct mbx_queued, node);
    }
    if ((flag & MBX_CHECK_NORMAL) && !list_empty(&c->normal)) {
        *instant = false;
        return list_first_entry(&c->normal, struct mbx_queued, node);
    }
    return NULL;
}

/* whether a class has a message of the flag's types (without the lock: a wait's condition) */
static bool mbx_has(struct mbx_class *c, u32 flag)
{
    return ((flag & MBX_CHECK_INSTANT) && READ_ONCE(c->queued[1])) ||
           ((flag & MBX_CHECK_NORMAL) && READ_ONCE(c->queued[0]));
}

static void mbx_dequeue(struct mbx_class *c, struct mbx_queued *q, bool instant)
{
    list_del(&q->node);
    kfree(q);
    WRITE_ONCE(c->queued[instant], c->queued[instant] - 1);
}

/* RECVMSG (remove) and CHECKMSG (look) of a class, as MStar's MDrv_MBX_RecvMsg and _CheckMsg:
 * RECVMSG takes one type, INSTANT or NORMAL, after waiting up to wait_ms for one; CHECKMSG is
 * INSTANT, NORMAL or both, of the class, or with both of every class the file owns (MStar's
 * MBX_CHECK_ALL_MSG_CLASS, instant ones first), and does not wait (its MBX_CHECK_BLOCK_RECV
 * fails MStar's own flag check). */
static u32 mbx_recv(struct mbx_file *f, u32 cls, struct mbx_msg *out, u32 wait_ms, u32 flag,
                    bool remove)
{
    struct mbx_class *c, *found = NULL;
    struct mbx_queued *q = NULL;
    bool instant = false;
    long waited = 0;
    u32 ret;

    if (!f->inited)
        return MBX_ERR_NOT_INITIALIZED;
    if (remove ? flag != MBX_CHECK_INSTANT && flag != MBX_CHECK_NORMAL
               : flag != MBX_CHECK_INSTANT && flag != MBX_CHECK_NORMAL && flag != MBX_CHECK_ALL)
        return MBX_ERR_INVALID_PARAM;
    if (cls >= MBX_CLASSES)
        return MBX_ERR_INVALID_PARAM;
    c = &mbx_classes[cls];
    if (remove)
        waited =
            wait_event_interruptible_timeout(mbx_wait, mbx_has(c, flag), msecs_to_jiffies(wait_ms));
    mutex_lock(&mbx_lock);
    if (!remove && flag == MBX_CHECK_ALL) {
        for (u32 pass = MBX_CHECK_INSTANT; pass <= MBX_CHECK_NORMAL && !q; pass <<= 1)
            for (int i = 0; i < MBX_CLASSES && !q; i++)
                if (mbx_classes[i].owner == f && (q = mbx_first(&mbx_classes[i], pass, &instant)))
                    found = &mbx_classes[i];
        ret = q ? MBX_SUCCESS : MBX_ERR_NO_MORE_MSG;
    } else if (!c->owner) {
        ret = MBX_ERR_SLOT_NOT_OPENED;
    } else {
        q = mbx_first(c, flag, &instant);
        found = c;
        ret = q ? MBX_SUCCESS : MBX_ERR_NO_MORE_MSG;
    }
    mbx_ev(remove ? MBX_OP_RECV : MBX_OP_CHECK, found ? found : c, f, q ? &q->msg : NULL, ret,
           remove ? waited : flag);
    if (q) {
        *out = q->msg;
        if (remove) {
            mbx_dequeue(found, q, instant);
        } else {
            mbx_checked.c = found;
            mbx_checked.instant = instant;
        }
    }
    mutex_unlock(&mbx_lock);
    return ret;
}

static int mbx_open(struct inode *inode, struct file *file)
{
    struct mbx_file *f = kzalloc(sizeof(*f), GFP_KERNEL);

    if (!f)
        return -ENOMEM;
    f->id = atomic_inc_return(&mbx_file_ids);
    file->private_data = f;
    mutex_lock(&mbx_lock);
    mbx_refcnt++;
    mbx_ev(MBX_OP_OPEN, NULL, f, NULL, 0, 0);
    mutex_unlock(&mbx_lock);
    return 0;
}

static int mbx_fasync(int fd, struct file *file, int on)
{
    struct mbx_file *f = file->private_data;

    return fasync_helper(fd, file, on, &f->fasync) < 0 ? -EBUSY : 0;
}

/* the file goes (MStar's _MDrv_MBXIO_Release: DeInit, pending messages discarded): its classes */
static int mbx_release(struct inode *inode, struct file *file)
{
    struct mbx_file *f = file->private_data;

    mbx_fasync(-1, file, 0);
    mutex_lock(&mbx_lock);
    for (int i = 0; i < MBX_CLASSES; i++) {
        struct mbx_class *c = &mbx_classes[i];

        if (c->owner == f) {
            int n = mbx_class_clear(c);

            mbx_ev(MBX_OP_RELEASE, c, f, NULL, 0, n);
            c->owner = NULL;
        }
    }
    mbx_ev(MBX_OP_RELEASE, NULL, f, NULL, 0, 0);
    mbx_refcnt--;
    mutex_unlock(&mbx_lock);
    kfree(f);
    return 0;
}

static long mbx_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    struct mbx_file *f = file->private_data;
    void __user *uarg = (void __user *)arg;
    union {
        struct mbx_init32 init;
        struct mbx_binfo32 binfo;
        struct mbx_register32 reg;
        struct mbx_unregister32 unreg;
        struct mbx_clear32 clear;
        struct mbx_send32 send;
        struct mbx_recv32 recv;
        struct mbx_msgq32 msgq;
        struct mbx_drvstatus drv;
        struct mbx_info32 info;
        u8 byte;
    } a = {};
    struct mbx_msg msg;
    struct mbx_class *c;
    size_t size = _IOC_SIZE(cmd);
    size_t wire_size = size;
    u8 wire[sizeof(a)];
    u32 *result = NULL;

    ioctl_count("msmailbox", cmd);
    if (_IOC_TYPE(cmd) != 'm' || size > sizeof(a))
        return unhandled("msmailbox", cmd, -ENOTTY);
    if (copy_from_user(wire, uarg, size))
        return -EFAULT;
    cmd = mbx_aligned_cmd(cmd);
    size = _IOC_SIZE(cmd);
    if (size != wire_size) {
        memcpy(&a, wire, wire_size - 4);
        memcpy((u8 *)&a + size - 4, wire + wire_size - 4, 4);
    } else {
        memcpy(&a, wire, size);
    }

    switch (cmd) {
    case MBX_IOC_INIT:
        result = &a.init.result;
        if (a.init.hk_cpu != MBX_CPU_AEON && a.init.hk_cpu != MBX_CPU_MIPS)
            *result = MBX_ERR_INVALID_PARAM;
        else if (a.init.host_role >= MBX_ROLES || !a.init.timeout_ms)
            *result = MBX_ERR_INVALID_PARAM;
        else
            f->inited = true, *result = MBX_SUCCESS;
        mbx_ev(MBX_OP_INIT, NULL, f, NULL, *result, 0);
        break;
    case MBX_IOC_DEINIT: /* bInfo: discard pending messages */
        result = &a.binfo.result;
        mutex_lock(&mbx_lock);
        *result = f->inited ? MBX_SUCCESS : MBX_ERR_NOT_INITIALIZED;
        for (int i = 0; i < MBX_CLASSES && f->inited; i++)
            if (mbx_classes[i].owner == f && !a.binfo.info &&
                mbx_classes[i].queued[0] + mbx_classes[i].queued[1])
                *result = MBX_ERR_HAS_MSG_PENDING;
        for (int i = 0; i < MBX_CLASSES && *result == MBX_SUCCESS; i++) {
            if (mbx_classes[i].owner == f) {
                int n = mbx_class_clear(&mbx_classes[i]);

                mbx_ev(MBX_OP_DEINIT, &mbx_classes[i], f, NULL, 0, n);
                mbx_classes[i].owner = NULL;
            }
        }
        mbx_ev(MBX_OP_DEINIT, NULL, f, NULL, *result, a.binfo.info);
        if (*result == MBX_SUCCESS && a.binfo.info)
            f->inited = f->enabled = false;
        mutex_unlock(&mbx_lock);
        break;
    case MBX_IOC_REGISTERMSG:
        result = &a.reg.result;
        if (!f->inited) {
            *result = MBX_ERR_NOT_INITIALIZED;
            break;
        }
        if (a.reg.cls >= MBX_CLASSES || !a.reg.queue_size || a.reg.queue_size > MBX_QUEUE_MAX) {
            *result = MBX_ERR_INVALID_PARAM;
            break;
        }
        c = &mbx_classes[a.reg.cls];
        mutex_lock(&mbx_lock);
        if (c->owner == f) {
            *result = MBX_ERR_SLOT_ALREADY_OPENED;
            mbx_ev(MBX_OP_REGISTER, c, f, NULL, *result, 0);
        } else {
            s32 prev = c->owner ? c->owner->id : 0;

            c->owner = f;
            c->queue_size = a.reg.queue_size;
            *result = MBX_SUCCESS;
            mbx_ev(prev ? MBX_OP_TAKEOVER : MBX_OP_REGISTER, c, f, NULL, *result, prev);
        }
        mutex_unlock(&mbx_lock);
        break;
    case MBX_IOC_UNREGISTERMSG:
    case MBX_IOC_CLEARMSG: {
        u32 cls = cmd == MBX_IOC_CLEARMSG ? a.clear.cls : a.unreg.cls;
        int n;

        result = cmd == MBX_IOC_CLEARMSG ? &a.clear.result : &a.unreg.result;
        if (!f->inited) {
            *result = MBX_ERR_NOT_INITIALIZED;
            break;
        }
        if (cls >= MBX_CLASSES) {
            *result = MBX_ERR_INVALID_PARAM;
            break;
        }
        c = &mbx_classes[cls];
        mutex_lock(&mbx_lock);
        n = 0;
        if (!c->owner)
            *result = MBX_ERR_SLOT_NOT_OPENED;
        else if (c->owner != f)
            *result = MBX_ERR_SLOT_BUSY;
        else if (cmd == MBX_IOC_UNREGISTERMSG && !a.unreg.force && c->queued[0] + c->queued[1])
            *result = MBX_ERR_HAS_MSG_PENDING;
        else {
            n = mbx_class_clear(c);
            if (cmd == MBX_IOC_UNREGISTERMSG)
                c->owner = NULL;
            *result = MBX_SUCCESS;
        }
        mbx_ev(cmd == MBX_IOC_CLEARMSG ? MBX_OP_CLEAR : MBX_OP_UNREGISTER, c, f, NULL, *result, n);
        mutex_unlock(&mbx_lock);
        break;
    }
    case MBX_IOC_SENDMSG:
    case MBX_IOC_SENDMSGLOOPBACK:
        result = &a.send.result;
        if (copy_from_user(&msg, compat_ptr(a.send.msg), sizeof(msg)))
            return -EFAULT;
        mutex_lock(&mbx_lock);
        /* traced before the answer it may bring; aux the target role */
        mbx_ev(MBX_OP_SEND, msg.cls < MBX_CLASSES ? &mbx_classes[msg.cls] : NULL, f, &msg, 0,
               msg.role);
        if (cmd == MBX_IOC_SENDMSGLOOPBACK && msg.role == MBX_ROLE_HK) {
            /* to this CPU itself, from the role in mbxResult */
            msg.role = a.send.result;
            mbx_arrive(&msg);
            *result = MBX_SUCCESS;
        } else {
            *result = mbx_send(&msg);
        }
        mutex_unlock(&mbx_lock);
        break;
    case MBX_IOC_RECVMSG:
    case MBX_IOC_CHECKMSG:
        result = &a.recv.result;
        *result =
            mbx_recv(f, a.recv.cls, &msg, a.recv.wait_ms, a.recv.flag, cmd == MBX_IOC_RECVMSG);
        if (*result == MBX_SUCCESS && copy_to_user(compat_ptr(a.recv.msg), &msg, sizeof(msg)))
            return -EFAULT;
        break;
    case MBX_IOC_REMOVEMSG: /* the message CHECKMSG last looked at */
        result = &a.binfo.result;
        mutex_lock(&mbx_lock);
        *result = MBX_ERR_NO_MORE_MSG;
        if (mbx_checked.c) {
            struct list_head *l =
                mbx_checked.instant ? &mbx_checked.c->instant : &mbx_checked.c->normal;

            if (!list_empty(l)) {
                struct mbx_queued *q = list_first_entry(l, struct mbx_queued, node);

                mbx_ev(MBX_OP_REMOVE, mbx_checked.c, f, &q->msg, MBX_SUCCESS, 0);
                mbx_dequeue(mbx_checked.c, q, mbx_checked.instant);
                *result = MBX_SUCCESS;
            }
        }
        if (*result != MBX_SUCCESS)
            mbx_ev(MBX_OP_REMOVE, NULL, f, NULL, *result, 0);
        mutex_unlock(&mbx_lock);
        break;
    case MBX_IOC_GETMSGQSTATUS: {
        struct mbx_msgq_status st = {};

        result = &a.msgq.result;
        if (!f->inited) {
            *result = MBX_ERR_NOT_INITIALIZED;
            break;
        }
        if (a.msgq.cls >= MBX_CLASSES) {
            *result = MBX_ERR_INVALID_PARAM;
            break;
        }
        c = &mbx_classes[a.msgq.cls];
        mutex_lock(&mbx_lock);
        if (!c->owner) {
            *result = MBX_ERR_SLOT_NOT_OPENED;
        } else if (c->owner != f) {
            *result = MBX_ERR_SLOT_BUSY;
        } else {
            st.instant = c->queued[1];
            st.normal = c->queued[0];
            st.status = (st.instant + st.normal >= c->queue_size ? MBX_STATUS_OVERFLOW : 0) |
                        (st.instant ? MBX_STATUS_INSTANT : 0) | (st.normal ? MBX_STATUS_NORMAL : 0);
            *result = MBX_SUCCESS;
        }
        mutex_unlock(&mbx_lock);
        if (*result == MBX_SUCCESS && copy_to_user(compat_ptr(a.msgq.status), &st, sizeof(st)))
            return -EFAULT;
        break;
    }
    case MBX_IOC_MBXENABLE:
        result = &a.binfo.result;
        mutex_lock(&mbx_lock);
        *result = f->inited ? MBX_SUCCESS : MBX_ERR_NOT_INITIALIZED;
        if (f->inited)
            f->enabled = a.binfo.info;
        mbx_ev(MBX_OP_ENABLE, NULL, f, NULL, *result, a.binfo.info);
        mutex_unlock(&mbx_lock);
        break;
    case MBX_IOC_SETINFORMATION:
    case MBX_IOC_GETINFORMATION:
        result = &a.info.result;
        if (a.info.role >= MBX_ROLES || a.info.size > MBX_INFO_SIZE) {
            *result = MBX_ERR_INVALID_PARAM;
            break;
        }
        mutex_lock(&mbx_lock);
        if (cmd == MBX_IOC_SETINFORMATION) {
            u8 info[MBX_INFO_SIZE];

            if (copy_from_user(info, compat_ptr(a.info.info), a.info.size)) {
                mutex_unlock(&mbx_lock);
                return -EFAULT;
            }
            memcpy(mbx_info[a.info.role], info, a.info.size);
        } else if (copy_to_user(compat_ptr(a.info.info), mbx_info[a.info.role], a.info.size)) {
            mutex_unlock(&mbx_lock);
            return -EFAULT;
        }
        mutex_unlock(&mbx_lock);
        *result = MBX_SUCCESS;
        break;
    case MBX_IOC_GETDRVSTATUS:
        mutex_lock(&mbx_lock);
        a.drv = (struct mbx_drvstatus){.enabled = f->enabled, .refcnt = mbx_refcnt};
        mutex_unlock(&mbx_lock);
        break;
    case PM_IOC_GET_BRICKTERMINATOR:
        a.byte = READ_ONCE(mbx_brick_terminator);
        break;
    case PM_IOC_SET_BRICKTERMINATOR:
        WRITE_ONCE(mbx_brick_terminator, a.byte);
        return 0;
    default:
        return unhandled("msmailbox", cmd, -ENOTTY);
    }
    if (size != wire_size) {
        memcpy(wire, &a, wire_size - 4);
        memcpy(wire + wire_size - 4, (u8 *)&a + size - 4, 4);
    } else {
        memcpy(wire, &a, size);
    }
    return copy_to_user(uarg, wire, wire_size) ? -EFAULT : 0;
}

static ssize_t tee_read(struct file *file, char __user *buf, size_t n, loff_t *pos)
{
    size_t size = ARRAY_SIZE(tee_seen) * 64, len = 0;
    char *text = kmalloc(size, GFP_KERNEL);
    ssize_t ret;

    if (!text)
        return -ENOMEM;
    mutex_lock(&mbx_lock);
    for (size_t i = 0; i < ARRAY_SIZE(tee_seen) && tee_seen[i].count; i++)
        len +=
            scnprintf(text + len, size - len, "%s %s %u: %lu, %s (first from %s)\n",
                      tee_seen[i].name, tee_seen[i].index ? "command" : "query", tee_seen[i].index,
                      tee_seen[i].count, tee_seen[i].status ? "done" : "failed", tee_seen[i].comm);
    mutex_unlock(&mbx_lock);
    ret = simple_read_from_buffer(buf, n, pos, text, len);
    kfree(text);
    return ret;
}

const struct file_operations mbx_fops = {
    .owner = THIS_MODULE,
    .open = mbx_open,
    .release = mbx_release,
    .fasync = mbx_fasync,
    .unlocked_ioctl = mbx_ioctl,
    .compat_ioctl = compat_ptr_ioctl,
};

static const struct proc_ops tee_proc_ops = {.proc_read = tee_read, .proc_lseek = default_llseek};
void mailbox_init(void)
{
    for (int i = 0; i < MBX_CLASSES; i++) {
        INIT_LIST_HEAD(&mbx_classes[i].normal);
        INIT_LIST_HEAD(&mbx_classes[i].instant);
    }
}
int mailbox_proc_create(struct proc_dir_entry *dir)
{
    if (!proc_create("tee", 0444, dir, &tee_proc_ops))
        return -ENOMEM;
    if (mbx_trace) {
        mbx_evs = vzalloc(array_size(MBX_TRACE_LEN, sizeof(*mbx_evs)));
        if (!mbx_evs || !proc_create_seq("mailbox", 0444, dir, &mbx_trace_seq_ops))
            return -ENOMEM;
    }
    return 0;
}
void mailbox_free(void)
{
    vfree(mbx_evs);
    mbx_evs = NULL;
}
