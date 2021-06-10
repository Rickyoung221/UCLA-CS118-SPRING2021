#ifndef __COMMON_H__
#define __COMMON_H__

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>

#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>
#include <netdb.h>
#include <sys/timerfd.h>
#include <sys/epoll.h>
#include <stdint.h>        /* Definition of uint64_t */

//#define __TCP_DEBUG__ 1
#ifdef __TCP_DEBUG__
#define TCP_LOG(__format, __args...) do {\
        printf("[%s-%d]:"__format, __FUNCTION__, __LINE__, ##__args);\
} while (0)
#else
#define TCP_LOG(__format, __args...)
#endif

//Epoll structure for tcp
/* Epoll: I/O event notification facility */
typedef struct tcp_epollop_struct {
	struct epoll_event *events;
	int nevents;
	int epfd;
} tcp_epollop;

//define global variables
#define RTO             50000      /* timeout in microseconds */
#define HDR_SIZE        12          /* header size*/
#define PKT_SIZE        524         /* total packet size */
#define PAYLOAD_SIZE    512         /* PKT_SIZE - HDR_SIZE */
#define WND_SIZE        10          /* window size */
#define MAX_SEQN        25601       /* number of sequence numbers [0-25600] */

//TCP State setting
enum {
  TCP_ESTABLISHED = 1,
  TCP_SYN_SENT,
  TCP_SYN_RECV,
  TCP_FIN_WAIT1,
  TCP_FIN_WAIT2,
  TCP_TIME_WAIT,
  TCP_CLOSE,
  TCP_CLOSE_WAIT,
  TCP_LAST_ACK,
  TCP_LISTEN,
  TCP_CLOSING,	 /* now a valid state */

  TCP_MAX_STATES /* Leave at the end! */
};

// Packet Structure: Described in Section 2.1.1 of the spec. DO NOT CHANGE!
struct packet {
    unsigned short seqnum;
    unsigned short acknum;
    char syn;
    char fin;
    char ack;
    char dupack;
    unsigned int length;
    char payload[PAYLOAD_SIZE];
};

/* get index, size of the queue (INTERNAL!!!) */
#define TCP_QUEUE_INDEX(q, i)      ((i) % ((q)->size))

/* empty, head and tail are equal */
#define TCP_QUEUE_EMPTY(q)         ((q)->head == (q)->tail)

/* full, not empty, but indexes are equal */
#define TCP_QUEUE_FULL(q)              \
        (!TCP_QUEUE_EMPTY(q)           \
             && (TCP_QUEUE_INDEX(q, (q)->head) == TCP_QUEUE_INDEX(q, (q)->tail)))

#define TCP_QUEUE_GET_SIZE(q)      ((q)->size)
#define TCP_QUEUE_GET_HEAD(q)      ((q)->head)
#define TCP_QUEUE_GET_TAIL(q)      ((q)->tail)

/* */
typedef struct tcp_queue_s {
    struct packet msg;
    char   processed;     /*  */
    struct timeval time;        /* time when the msg enter queue */
    int time_fd;                /* Ê */
} tcp_queue;

/* Queue management node */
typedef struct tcp_queue_head_s {
    unsigned int   size;          /* The size of the queue */
    unsigned int   head;          /* Head of queue */
    unsigned int   tail;          /* Tail of queue */
    tcp_queue      *node;         /* Buffer of msg */
} tcp_queue_head;

struct tcp_manage_s {

    unsigned short seqnum;
    unsigned short acknum;

    int state;//TCP status, see definition above
    tcp_queue_head *txqueue;

    tcp_epollop eop;

    FILE *fp;
    int time_wait_fd;
};

//Initialize timerfd, create and operate the timer through file descriptor.
static inline int tcp_timer_fd_init(int *fd, int init_timer, int timer_interval)
{
    int sec, ms;
    struct itimerspec new_value;
    struct timespec now;
    //timerfd_create: creates a new timer object, and returns a file descriptor that refers to that timer.
    *fd = timerfd_create(CLOCK_REALTIME, 0);
    if (*fd == -1) {
        perror("timerfd_create");
        return -1;
    }

    memset(&new_value, 0x00, sizeof(new_value));
    clock_gettime(CLOCK_REALTIME, &now);

    sec = init_timer / 1000;
    ms = init_timer % 1000;
    new_value.it_value.tv_sec = now.tv_sec + sec;
    new_value.it_value.tv_nsec = now.tv_nsec + (ms * 1000 *1000);
    if (new_value.it_value.tv_nsec >= 1000000000) {
        new_value.it_value.tv_sec++;
        new_value.it_value.tv_nsec -= 1000000000;
    }

    if (timer_interval) {
        sec = timer_interval / 1000;
        ms = timer_interval % 1000;
        new_value.it_interval.tv_sec = sec;
        new_value.it_interval.tv_nsec = ms * 1000 *1000;
        if (new_value.it_interval.tv_nsec >= 1000000000) {
            new_value.it_interval.tv_sec++;
            new_value.it_interval.tv_nsec -= 1000000000;
        }
    }
    
    //timerfd_settime: arms or disarms the timer identified by timerid.
    if (timerfd_settime(*fd, TFD_TIMER_ABSTIME, &new_value, NULL) == -1) {
        perror("timerfd_settime");
        return -1;
    }

    return 0;
}


//
static inline void tcp_set_fd_nonblock(int fd)
{
	int flags;

	if ((flags = fcntl(fd, F_GETFL, NULL)) < 0) {
		return;
	}

	if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
		return;
	}

	return;
}

//
static inline void tcp_epoll_add_fd(int epollfd, int fd)
{
    struct epoll_event event;

    tcp_set_fd_nonblock(fd);

    event.data.fd = fd;
    event.events = EPOLLIN;
    //ADD: Add a file descriptor to the interface.
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);

	return;
}

static inline void tcp_epoll_del_fd(int epollfd, int fd)
{
    //DEL: Remove a file descriptor from the interface.
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, NULL);

	close(fd);

	return;
}

// Create the TCP sender queue
static inline tcp_queue_head *tcp_queue_create(unsigned short size)
{
    tcp_queue_head *q = NULL;

    if (((q = (tcp_queue_head *)malloc(sizeof(*q))) == NULL)
            || ((q->node = (tcp_queue *)malloc(sizeof(tcp_queue) * size)) == NULL)) {
        TCP_LOG("queue creat out of memory!");
        if (q != NULL) {
            free(q);
        }

        q = NULL;
    } else {
        q->size = size;
        q->head = 0;
        q->tail = 0;
    }

    return q;
}

/* Checks three numbers for low <= mid < high.  Checks for rollover. */
int tcp_queue_range_ok(unsigned short low, unsigned short mid, unsigned short high)
{
    int ok;

    /* if we haven't rolled over... */
    if (low < high) {
        ok = (low <= mid) && (mid < high);
    } else {                           /* roll over case */
        ok = (low <= mid) || (mid < high);
    }

    return ok;
}

/* Check whether it is within the window */
int tcp_queue_valid(tcp_queue_head *q, unsigned short num)
{
    int status;

    if (TCP_QUEUE_EMPTY(q)) {
        status = 0;
    } else {
        status = tcp_queue_range_ok(q->head, num, q->tail);
    }

    return status;
}

/* add a new element to the queue */
static inline int tcp_queue_add(tcp_queue_head *q, struct packet *msg)
{
    tcp_queue *node;
    int ret = -1;
    unsigned int index;

    if (!TCP_QUEUE_FULL(q)) {
        index = TCP_QUEUE_INDEX(q, q->tail);
        node = &q->node[index];

        memcpy(&node->msg, msg, sizeof(struct packet));
        node->processed = 0;

        if (node->time_fd == 0) {
            tcp_timer_fd_init(&node->time_fd, 500, 500);
        }

        q->tail++;

        TCP_LOG("queue index %u time fd %d, tail to %u\n", index,
            node->time_fd, q->tail);

        ret = node->time_fd;
    } else {
        TCP_LOG("queue head %u, tail %u, queue is full\n", q->head, q->tail);
    }

    return ret;
}

// Get the node of the queue
int tcp_queue_get_qnode(
    tcp_queue_head *q,
    unsigned short num, /* number of msg to get */
    tcp_queue **node  /* returned reference to msg */
)
{
    if (tcp_queue_valid(q, num))
    {
        *node = &q->node[TCP_QUEUE_INDEX(q, num)];
        return 1;
    }
    else
    {
        *node = NULL;
        return 0;
    }
}

// Delete the header of the queue
int tcp_queue_rem(tcp_queue_head *q, int epollfd)
{
    tcp_queue *node;
    unsigned int head;
    int ret = -1;

    if (!TCP_QUEUE_EMPTY(q))
    {
        head = TCP_QUEUE_INDEX(q, q->head);
        node = &q->node[head];
        node->processed = 0;
        /* Cancel timer */
        tcp_epoll_del_fd(epollfd, node->time_fd);
        node->time_fd = 0;

        q->head++;
        ret = 0;
        TCP_LOG("tcp_queue_rem queue del head %u, head to %u\n", head, q->head);
    } else {
        TCP_LOG("tcp_queue_rem queue is empty\n");
    }

    return ret;
}

// Add seqNum. Care the case of overflow.
static inline void tcp_seqnum_add_cnt(struct tcp_manage_s *tm, unsigned int cnt)
{
    tm->seqnum = (tm->seqnum + cnt) % MAX_SEQN;
    return;
}
// Add ackNum
static inline void tcp_acknum_add_cnt(struct tcp_manage_s *tm, unsigned int cnt)
{
    tm->acknum = (tm->acknum + cnt) % MAX_SEQN;
    return;
}

// ==============================
// Printing Functions: Call them on receiving/sending/packet timeout according
// Section 2.6 of the spec. The content is already conformant with the spec,
// no need to change. Only call them at correct times.
static inline void printRecv(struct packet *pkt)
{
    printf("RECV %d %d%s%s%s\n", pkt->seqnum, pkt->acknum,
        pkt->syn ? " SYN": "",
        pkt->fin ? " FIN": "",
        (pkt->ack || pkt->dupack) ? " ACK": "");
}

static inline void printSend(struct packet *pkt, int resend)
{
    if (resend)
        printf("RESEND %d %d%s%s%s\n", pkt->seqnum, pkt->acknum,
            pkt->syn ? " SYN": "",
            pkt->fin ? " FIN": "",
            pkt->ack ? " ACK": "");
    else
        printf("SEND %d %d%s%s%s%s\n", pkt->seqnum, pkt->acknum,
            pkt->syn ? " SYN": "",
            pkt->fin ? " FIN": "",
            pkt->ack ? " ACK": "",
            pkt->dupack ? " DUP-ACK": "");
}

static inline void printTimeout(struct packet* pkt)
{
    printf("TIMEOUT %d\n", pkt->seqnum);
}

// Building a packet by filling the header and contents.
// This function is provided to you and you can use it directly
static inline void buildPkt(struct packet* pkt,
        unsigned short seqnum,
        unsigned short acknum,
        char syn, char fin, char ack, char dupack,
        unsigned int length, char *payload)
{
    memset(pkt, 0x00, sizeof(struct packet));

    pkt->seqnum = seqnum;
    pkt->acknum = acknum;
    pkt->syn = syn;
    pkt->fin = fin;
    pkt->ack = ack;
    pkt->dupack = dupack;
    pkt->length = length;

    if (payload && length) {
        memcpy(pkt->payload, payload, length);
    }

    return;
}
// ==============================

// ==============================
// A series functions to enumerate and send the packet for various type of msg.
static inline void tcp_build_syn_pkt(struct packet *pkt, unsigned short seqnum)
{
    buildPkt(pkt, seqnum, 0, 1, 0, 0, 0, 0, NULL);
    return;
}

static inline void tcp_build_synack_pkt(struct packet *pkt, unsigned short seqnum, unsigned short acknum)
{
    buildPkt(pkt, seqnum, acknum, 1, 0, 1, 0, 0, NULL);
    return;
}

static inline void tcp_build_fin_pkt(struct packet *pkt, unsigned short seqnum, unsigned short acknum)
{
    buildPkt(pkt, seqnum, acknum, 0, 1, 0, 0, 0, NULL);
    return;
}

static inline void tcp_build_ack_pkt(struct packet *pkt, unsigned short seqnum, unsigned short acknum)
{
    buildPkt(pkt, seqnum, acknum, 0, 0, 1, 0, 0, NULL);
    return;
}

static inline void tcp_build_ack_data_pkt(struct packet *pkt, unsigned short seqnum,
        unsigned short acknum, unsigned int length, char *payload)
{
    buildPkt(pkt, seqnum, acknum, 0, 0, 1, 0, length, payload);
    return;
}
// ===================================
// TCP, Send packet
static inline int tcp_send_pkt(int fd, struct packet *pkt, struct sockaddr_in *sockaddr, int resend)
{
    int client_len = sizeof(struct sockaddr_in);
    int ret;

    printSend(pkt, resend);

    ret = sendto(fd, pkt, PKT_SIZE, 0, (struct sockaddr *)sockaddr, client_len);
    if (ret < 0) {
        perror("sendto error:");
        return -1;
    }

    return 0;
}

// add packed in queue
static inline int tcp_queue_add_pkt(tcp_queue_head *q, struct packet *pkt)
{
    int ret;

    if ((pkt->ack || pkt->dupack)
            && pkt->syn == 0
            && pkt->fin == 0
            && pkt->length == 0) {
        TCP_LOG("only ack pkt not queue\n");
        return -1;
    }

    ret = tcp_queue_add(q, pkt);
    if (ret == -1) {
        return -1;
    }

    return ret;
}

// =====================================
static inline double setTimer(void)
{
    struct timeval e;
    gettimeofday(&e, NULL);
    return (double) e.tv_sec + (double) e.tv_usec/1000000 + (double) RTO/1000000;
}

static inline int isTimeout(double end)
{
    struct timeval s;
    gettimeofday(&s, NULL);
    double start = (double) s.tv_sec + (double) s.tv_usec/1000000;
    return ((end - start) < 0.0);
}

// =====================================

//initialize EPOLL
static inline int tcp_epoll_init(tcp_epollop *eop, int nevent)
{
    int len;

    eop->nevents = nevent;
    len = eop->nevents * sizeof(struct epoll_event);
	eop->events = malloc(len);
	if (eop->events == NULL) {
		printf("dns epoll creat get memory error\n");
		return -1;
	}

    memset(eop->events, 0x00, len);
	eop->epfd = epoll_create(500);//Random value, don¡¯t care
	if (eop->epfd == -1) {
        free(eop->events);
        eop->events = NULL;
		perror("epoll creat error");
		return -1;
	}

    return 0;
}

#endif /* __COMMON_H__ */
