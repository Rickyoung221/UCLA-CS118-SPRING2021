#include "common.h"

#define FILE_NAME_LEN   31

unsigned int r_seq = 1;
int g_server_sockfd;
struct tcp_manage_s g_tcp_server_man;
struct packet g_synack_pkt;
struct packet g_finack_pkt;
struct sockaddr_in cliaddr;

// =====================================
// Initialize UDP socket, wait for client to connect
// Socket setup
int tcp_init_socket(unsigned short port)
{
    struct sockaddr_in servaddr;

    g_server_sockfd = socket(AF_INET, SOCK_DGRAM, 0);
	if (g_server_sockfd < 0){
		perror("socket error");
		return -1;
	}

    memset(&servaddr, 0x00, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(port);

    if (bind(g_server_sockfd, (struct sockaddr *) &servaddr, sizeof(servaddr)) == -1) {
        perror("bind() error");
        return -1;
    }

    return 0;
}

// receive the pkt, write it into file
void tcp_server_write_file(char *buf, int len)
{
    fwrite(buf, 1, len, g_tcp_server_man.fp);
    fflush(g_tcp_server_man.fp);

    return;
}

// Timer times out. The server transmit the msg via timer FD.
// Check the unack msg in queue, compare the value of FD, if equal -> resent the msg of this FD.
void tcp_server_tx_retry(int fd, tcp_queue_head *txqueue)
{
    unsigned int i;
    unsigned short start, num;
    tcp_queue *node;

    start = TCP_QUEUE_GET_HEAD(txqueue);
    TCP_LOG("tcp server queue head %u\n", start);

    /* Traverse the queue */
    for (i = 0; i < txqueue->size; i++) {
        num = start + i;

        if (tcp_queue_valid(txqueue, num) == 0) {
            TCP_LOG("queue num %u invalid\n", num);
            break;
        }

        (void)tcp_queue_get_qnode(txqueue, num, &node);
        if (node == NULL) {
            TCP_LOG("queue num %u get msg NULL\n", num);
            continue;
        }

        if (node->time_fd != fd) {
            continue;
        }

		printTimeout(&node->msg);
        tcp_send_pkt(g_server_sockfd, &node->msg, &cliaddr, 1);
    }

    return;
}

// Receive the ackNum, delete all the msg which have smaller ackNum in the queue.
// Cancel the timer.
void tcp_client_tx_confirm(tcp_queue_head *txqueue, unsigned short ack)
{
    unsigned int i;
    unsigned short start, num;
    tcp_queue *node;

    start = TCP_QUEUE_GET_HEAD(txqueue);
    TCP_LOG("tx_confirm queue head %u, ack %u\n", start, ack);

    /* Traverse the queue */
    for (i = 0; i < txqueue->size; i++) {
        num = start + i;

        if (tcp_queue_valid(txqueue, num) == 0) {
            TCP_LOG("tx_confirm queue num %u invalid\n", num);
            break;
        }

        (void)tcp_queue_get_qnode(txqueue, num, &node);
        if (node == NULL) {
            TCP_LOG("tx_confirm queue num %u get msg NULL\n", num);
            continue;
        }

        TCP_LOG("tx_confirm queue num %u seqnum %u\n", num, node->msg.seqnum);
        if (node->msg.seqnum < ack) {
            //The seq in the queue must be from small to large, so delete from the beginning
            tcp_queue_rem(txqueue, g_tcp_server_man.eop.epfd);
		} else {//ack flip
            if (ack < MAX_SEQN / 2 && node->msg.seqnum > MAX_SEQN / 2) {
                TCP_LOG("num %u seqnum %u, ack %d, turn !!!!\n", num, node->msg.seqnum, ack);
                tcp_queue_rem(txqueue, g_tcp_server_man.eop.epfd);
            } else {
                TCP_LOG("NOT ACK, BREAK\n");
                break;
            }
        }
    }

    return;
}

// Emunerate TCP state transmition diagram
int tcp_rcv_state_process(int fd)
{
    int readn, cliaddrlen, ret;

    struct packet __recvpkt;
    struct packet __sendpkt;
    struct packet *recvpkt, *sendpkt;

    char file_name[FILE_NAME_LEN + 1];

    recvpkt = &__recvpkt;
    sendpkt = &__sendpkt;

    cliaddrlen = sizeof(cliaddr);

    /* fd is readable, get packet */
    readn = recvfrom(fd, recvpkt, PKT_SIZE, 0, (struct sockaddr *)&cliaddr,
                (socklen_t *)&cliaddrlen);
    if (readn < 0) {
        perror("recvfrom error\n");
        return -1;
    } else if (readn == 0) {
        TCP_LOG("client fd %d over\n", fd);
        return 1;
    }

    printRecv(recvpkt);

    TCP_LOG("server recv new pkt: tcp state is %d, our seqnum %u, acknum %u, recv length %u\n",
        g_tcp_server_man.state, g_tcp_server_man.seqnum, g_tcp_server_man.acknum,
        recvpkt->length);

    if (g_tcp_server_man.state == TCP_ESTABLISHED) {
        TCP_LOG("tcp state is ESTABLISHED, recv pkt trans file\n");
        /* Compare the seqNum to see if it is what we expect to receive
        * If not reply ACK
        */
        if (g_tcp_server_man.acknum != recvpkt->seqnum) {
            TCP_LOG("tcp state is ESTABLISHED, our ack %u != pkt seqnum %u\n",
                g_tcp_server_man.acknum, recvpkt->seqnum);

            tcp_build_ack_pkt(sendpkt, g_tcp_server_man.seqnum, g_tcp_server_man.acknum);
            tcp_send_pkt(g_server_sockfd, sendpkt, &cliaddr, 0);
        } else {
            TCP_LOG("tcp state is ESTABLISHED, our ack %u == pkt seqnum %u, write file\n",
                g_tcp_server_man.acknum, recvpkt->seqnum);

            if (recvpkt->length) {
                tcp_server_write_file(recvpkt->payload, recvpkt->length);
                tcp_acknum_add_cnt(&g_tcp_server_man, recvpkt->length);

                TCP_LOG("tcp state is ESTABLISHED, our ack to %u\n", g_tcp_server_man.acknum);

                tcp_build_ack_pkt(sendpkt, g_tcp_server_man.seqnum, g_tcp_server_man.acknum);
                tcp_send_pkt(g_server_sockfd, sendpkt, &cliaddr, 0);
            }
        }

        if (recvpkt->fin) {
            /* Receive FIN, reply ACK */
            g_tcp_server_man.acknum = (recvpkt->seqnum + 1) % MAX_SEQN;
            tcp_build_ack_pkt(&g_finack_pkt, g_tcp_server_man.seqnum, g_tcp_server_man.acknum);

            tcp_send_pkt(g_server_sockfd, &g_finack_pkt, &cliaddr, 0);
            g_tcp_server_man.state = TCP_CLOSE_WAIT;
            fflush(g_tcp_server_man.fp);
            fclose(g_tcp_server_man.fp);

            /* Send FIN */
            tcp_build_fin_pkt(sendpkt, g_tcp_server_man.seqnum, g_tcp_server_man.acknum);
            tcp_send_pkt(g_server_sockfd, sendpkt, &cliaddr, 0);
            tcp_seqnum_add_cnt(&g_tcp_server_man, 1);

            ret = tcp_queue_add_pkt(g_tcp_server_man.txqueue, sendpkt);
            if (ret == -1) {
                TCP_LOG("FIN add queue pkt error\n");
                return -1;
            }
            tcp_epoll_add_fd(g_tcp_server_man.eop.epfd, ret);

            g_tcp_server_man.state = TCP_LAST_ACK;
        }

        return 0;
    }

    switch (g_tcp_server_man.state) {
    case TCP_LISTEN:/* Process the syn request message */
        if (recvpkt->syn == 0) {
            TCP_LOG("tcp state is LISTEN, recv pkt not syn, drop it\n");
            break;
        }

        /* Reply to syn+ack message */
        srand(time(NULL));
        g_tcp_server_man.seqnum = (rand() + 0xabcd) % MAX_SEQN;
        g_tcp_server_man.acknum = (recvpkt->seqnum + 1) % MAX_SEQN;
        TCP_LOG("server init seqnum %u, acknum %u\n", g_tcp_server_man.seqnum,
            g_tcp_server_man.acknum);

        tcp_build_synack_pkt(&g_synack_pkt, g_tcp_server_man.seqnum, g_tcp_server_man.acknum);
        tcp_send_pkt(g_server_sockfd, &g_synack_pkt, &cliaddr, 0);
        ret = tcp_queue_add_pkt(g_tcp_server_man.txqueue, &g_synack_pkt);
        if (ret == -1) {
            TCP_LOG("syn & ack add queue pkt error\n");
            return -1;
        }
        tcp_epoll_add_fd(g_tcp_server_man.eop.epfd, ret);
        tcp_seqnum_add_cnt(&g_tcp_server_man, 1);//·¢ËÍÐòÁÐºÅ+1

        g_tcp_server_man.state = TCP_SYN_RECV;

        TCP_LOG("tcp send syn & ack, state to SYN_RECV\n");
        break;

    case TCP_SYN_RECV: /* Handling the ack of the three-way handshake */
        if (recvpkt->syn) {//Retransmit SYN from the peer
            TCP_LOG("tcp state SYN_RECV, recv syn pkt, send syn & ack pkt\n");
            //Reply directly to syn&ack
            tcp_send_pkt(g_server_sockfd, &g_synack_pkt, &cliaddr, 0);
            break;
        }

        if (recvpkt->ack == 0) {
            TCP_LOG("tcp state is SYN_RECV, recv pkt not ack, drop it\n");
            break;
        }

        if (g_tcp_server_man.acknum == recvpkt->seqnum
                && g_tcp_server_man.seqnum  == recvpkt->acknum) {

            g_tcp_server_man.state = TCP_ESTABLISHED;

            tcp_client_tx_confirm(g_tcp_server_man.txqueue, recvpkt->acknum);

            TCP_LOG("tcp state is SYN_RECV, recv ack, state to ESTABLISHED\n");

            if (recvpkt->length) {
                memset(file_name, 0x00, FILE_NAME_LEN + 1);
                snprintf(file_name, 31, "%d.file", r_seq);

                g_tcp_server_man.fp = fopen(file_name, "w+");
                if (g_tcp_server_man.fp == NULL) {
                    perror("ERROR: File could not be created\n");
                    exit(1);
                }

                tcp_server_write_file(recvpkt->payload, recvpkt->length);

                tcp_acknum_add_cnt(&g_tcp_server_man, recvpkt->length);
                tcp_build_ack_pkt(sendpkt, g_tcp_server_man.seqnum, g_tcp_server_man.acknum);
                tcp_send_pkt(g_server_sockfd, sendpkt, &cliaddr, 0);
            }
        } else {
            TCP_LOG("tcp state is SYN_RECV, recv pkt not we want seq pkt\n");
        }

        break;

    case TCP_CLOSE_WAIT:
    case TCP_LAST_ACK:
        if (recvpkt->fin) {
            //The peer did not receive the ack, and the retransmitted ack
            tcp_send_pkt(g_server_sockfd, &g_finack_pkt, &cliaddr, 0);
            break;
        }

        if (recvpkt->acknum == g_tcp_server_man.seqnum) {

            tcp_client_tx_confirm(g_tcp_server_man.txqueue, recvpkt->acknum);

            TCP_LOG("tcp state is TCP_CLOSE_WAIT, recv Fin ack ,state to CLOSE\n");
            g_tcp_server_man.state = TCP_CLOSE;

            return 1;
        } else {
            TCP_LOG("tcp state is TCP_CLOSE_WAIT, recv Fin ack, seq error???\n");
        }
        break;

    default:
        break;
    }

    return 0;
}


//Epoll, wait for I/O events. blocking the calling thread if no events are currently available
//
int tcp_server_socket_epoll(struct epoll_event* events, int number)
{
	int i, ret, s;
	int fd;
    uint64_t exp;

    /* Handling fd events */
    for (i = 0; i < number; i++ ) {
        fd = events[i].data.fd;
        if (fd == g_server_sockfd) {
            if (events[i].events & EPOLLIN) {
                ret = tcp_rcv_state_process(g_server_sockfd);
                if (ret == 1) {
                    TCP_LOG("one client running end\n");
                    return 1;
                }
            } else {
                TCP_LOG("fd %d event not have EPOLLIN\n", fd);
            }
        } else {//Packet timer timeout
            s = read(fd, &exp, sizeof(uint64_t));
            if (s != sizeof(uint64_t)) {
                TCP_LOG("read timetout fd %d error\n", fd);
            }
            tcp_server_tx_retry(fd, g_tcp_server_man.txqueue);
        }
    }

	return 0;
}

int main (int argc, char *argv[])
{
    unsigned short servPort;
    int ret, e_ret;

    if (argc != 2) {
        printf("ERROR: incorrect number of arguments\n");
        return -1;
    }

    g_tcp_server_man.txqueue = tcp_queue_create(WND_SIZE);
    if (g_tcp_server_man.txqueue == NULL) {
        return -1;
    }

    servPort = atoi(argv[1]);
    if (tcp_init_socket(servPort) == -1) {
        return -1;
    }

    if (tcp_epoll_init(&g_tcp_server_man.eop, 32) == -1) {
        return -1;
    }

    tcp_epoll_add_fd(g_tcp_server_man.eop.epfd, g_server_sockfd);

    for (; r_seq < 0xFFFFFFFF; r_seq++) {
        g_tcp_server_man.state = TCP_LISTEN;

        /* Waiting for client to connect */
    	while (1) {
            ret = epoll_wait(g_tcp_server_man.eop.epfd, g_tcp_server_man.eop.events,
                        g_tcp_server_man.eop.nevents, -1);
            if (ret < 0 ) {
                perror( "epoll wait error\n" );
                break;
            }

            /* Handling epoll events */
            e_ret = tcp_server_socket_epoll(g_tcp_server_man.eop.events, ret);
            if (e_ret) {
                break;
            }
        }
    }
}

