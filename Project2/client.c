
#include "common.h"

int client_sockfd;
struct tcp_manage_s g_client_tcp_man;
struct sockaddr_in servaddr;
FILE *fp;
struct packet fin_last_ackpkt;

int file_send_over = 0;

//Client side, when the timer times out, retransmit the msg.
//Check the unacked msg, then compare the FD, if it equal, then resend the msg.
void tcp_client_tx_retry(int fd, tcp_queue_head *txqueue)
{
    unsigned int i;
    unsigned short start, num;
    tcp_queue *node;

    start = TCP_QUEUE_GET_HEAD(txqueue);
    TCP_LOG("tcp client queue head %u\n", start);

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
        tcp_send_pkt(client_sockfd, &node->msg, &servaddr, 1);
    }

    return;
}

//After receive the ACK message, delete the messages in the queue, delete the messages which are smaller than the ACKnum, and cancel the timer
//The seq# may filp
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
            TCP_LOG("num %u seqnum %u < ack %d, confirm!!\n", num, node->msg.seqnum, ack);
            //The seq in the queue must be from small to large, so delete delete from the beginning
            tcp_queue_rem(txqueue, g_client_tcp_man.eop.epfd);
        } else {//ack flip
            if (ack < MAX_SEQN / 2 && node->msg.seqnum > MAX_SEQN / 2) {
                TCP_LOG("client num %u seqnum %u, ack %d, turn !!!!\n", num, node->msg.seqnum, ack);
                tcp_queue_rem(txqueue, g_client_tcp_man.eop.epfd);
            } else {
                TCP_LOG("client NOT ACK, BREAK\n");
                break;
            }
        }
    }

    return;
}

//TCP send FIN
int tcp_client_do_fin(int fd)
{
    int ret;
    struct packet finpkt;

    if (fp) {
        fclose(fp);
        fp = NULL;
    }

    tcp_build_fin_pkt(&finpkt, g_client_tcp_man.seqnum, g_client_tcp_man.acknum);
    tcp_send_pkt(fd, &finpkt, &servaddr, 0);
    tcp_seqnum_add_cnt(&g_client_tcp_man, 1);
    g_client_tcp_man.state = TCP_FIN_WAIT1;
    TCP_LOG("client tcp state to TCP_FIN_WAIT1, seqnum %u\n", g_client_tcp_man.seqnum);

    ret = tcp_queue_add_pkt(g_client_tcp_man.txqueue, &finpkt);
    if (ret == -1) {
        TCP_LOG("fin add queue pkt error\n");
        return -1;
    }
    tcp_epoll_add_fd( g_client_tcp_man.eop.epfd, ret);

    return 0;
}

// send pkt based on the size of the window until it is full.
// the msg send need to add in the queue,
// retransmitted after timeout
// remove after the msg is acked.
// Initial window size is 10
int tcp_client_send_winsize_pkt(int fd)
{
    struct packet sendPkt;
    char buf[PAYLOAD_SIZE];
    int readn, ret;

    while (1) {
        if (TCP_QUEUE_FULL(g_client_tcp_man.txqueue)) {
            //The queue is full, wait for ACK and send a message next time
            TCP_LOG("queue is FULL, send next\n");
            break;
        }

        TCP_LOG("queue NOT FULL, send pkt\n");

        readn = fread(buf, 1, PAYLOAD_SIZE, fp);
		if (readn == 0) {
			TCP_LOG("read pkt len %d, to break\n", readn);
			file_send_over = 1;
            break;
		}
		
        TCP_LOG("send windows pkt len %d\n", readn);

        tcp_build_ack_data_pkt(&sendPkt, g_client_tcp_man.seqnum, g_client_tcp_man.acknum, readn, buf);
        tcp_send_pkt(fd, &sendPkt, &servaddr, 0);
        tcp_seqnum_add_cnt(&g_client_tcp_man, readn);

        TCP_LOG("client seqnum to %u\n", g_client_tcp_man.seqnum);

        ret = tcp_queue_add_pkt(g_client_tcp_man.txqueue, &sendPkt);
        if (ret == -1) {
            TCP_LOG("not happen!!!!\n");
            return -1;
        }
        tcp_epoll_add_fd( g_client_tcp_man.eop.epfd, ret);

        /* Read to the end of the file */
        if (readn < PAYLOAD_SIZE) {
            file_send_over = 1;
            break;
        }
    }

    return 0;
}

int tcp_client_rcv_state_process(int fd)
{
    int readn, ser_addrlen;

    struct packet __recvpkt;
    struct packet *recvpkt;

    recvpkt = &__recvpkt;
    ser_addrlen = sizeof(struct sockaddr_in);

    /* fd is readable, get packet */
    readn = recvfrom(fd, recvpkt, PKT_SIZE, 0, (struct sockaddr *)&servaddr,
                (socklen_t *)&ser_addrlen);
    if (readn < 0) {
        perror("recvfrom error\n");
        return -1;
    }

    printRecv(recvpkt);

    TCP_LOG("client recv new pkt: tcp state is %d, our seqnum %u, acknum %u\n", g_client_tcp_man.state,
        g_client_tcp_man.seqnum, g_client_tcp_man.acknum);

    if (g_client_tcp_man.state == TCP_ESTABLISHED) {
        TCP_LOG("client tcp state is ESTABLISHED, recv pkt trans file ack\n");

		/* Compare the serial number to see if it is what we expect to receive
		 * If not reply ACK
		 */
        if (recvpkt->ack == 0) {
            TCP_LOG("client tcp state is ESTABLISHED, not ack type??\n");
            return 0;
        }

        /* Cumulative confirmation */
        tcp_client_tx_confirm(g_client_tcp_man.txqueue, recvpkt->acknum);

        /* Determine whether the ACK is in the window we sent, and if it is, delete the sent message to avoid retransmission */
        if (recvpkt->acknum <= g_client_tcp_man.seqnum) {
            g_client_tcp_man.acknum = (g_client_tcp_man.acknum + recvpkt->length) % MAX_SEQN;
            TCP_LOG("client acknum to %u\n", g_client_tcp_man.acknum);
            //Has a data message been received?
            if (recvpkt->length) {
                TCP_LOG("client tcp state is ESTABLISHED, recv pkt length %d\n", recvpkt->length);
            }
        }

        if (file_send_over) {
            TCP_LOG("client tcp state is ESTABLISHED, send file over\n");
            if (TCP_QUEUE_EMPTY(g_client_tcp_man.txqueue)) {
                TCP_LOG("TXQUEUE EMPTY, send FIN to close client\n");
                tcp_client_do_fin(fd);
            }
        } else {
            tcp_client_send_winsize_pkt(fd);
        }

        return 0;
    }

    switch (g_client_tcp_man.state) {
    case TCP_SYN_SENT:/* Process syn+ack message */
        if (recvpkt->syn == 0) {
            TCP_LOG("tcp state is TCP_SYN_SENT, recv pkt not syn, drop it\n");
            break;
        }

        if (recvpkt->ack == 0) {
            TCP_LOG("tcp state is TCP_SYN_SENT, recv pkt not ave ack, drop it\n");
            break;
        }

        if ((recvpkt->ack || recvpkt->dupack)
                && recvpkt->acknum == g_client_tcp_man.seqnum) {
            //Initialize our local acknum to the seqnum + 1 of the received syn message
            g_client_tcp_man.acknum = (recvpkt->seqnum + 1) % MAX_SEQN;
            g_client_tcp_man.state = TCP_ESTABLISHED;

            /* Processing queue confirmation message */
            tcp_client_tx_confirm(g_client_tcp_man.txqueue, recvpkt->acknum);

            TCP_LOG("tcp recv syn & ack, state to TCP_ESTABLISHED, acknum %u\n", g_client_tcp_man.acknum);

            /* Reply ACK + send window pipeline data segment */
            tcp_client_send_winsize_pkt(fd);
        } else {
            TCP_LOG("tcp state is TCP_SYN_SENT, recv pkt not we want seq pkt\n");
        }

        break;

    case TCP_FIN_WAIT1:
        //According to the agreement, only ACK messages will be received
        if (recvpkt->ack == 0) {
            TCP_LOG("tcp state is TCP_FIN_WAIT1, recv pkt not ave ack, drop it\n");
            break;
        }

        if ((recvpkt->ack || recvpkt->dupack)
                && recvpkt->acknum == g_client_tcp_man.seqnum) {
            g_client_tcp_man.state = TCP_FIN_WAIT2;

            //Cancel FIN retransmission
            tcp_client_tx_confirm(g_client_tcp_man.txqueue, recvpkt->acknum);
        } else {
            TCP_LOG("tcp state is TCP_FIN_WAIT1, recv pkt not we want seq pkt\n");
        }
        break;

     case TCP_FIN_WAIT2:
        //According to the agreement, only FIN messages will be received
        if (recvpkt->fin == 0) {
            TCP_LOG("tcp state is TCP_FIN_WAIT2, recv pkt not have fin, drop it\n");
            break;
        }

        if (recvpkt->acknum == g_client_tcp_man.seqnum) {
            g_client_tcp_man.state = TCP_TIME_WAIT;
            tcp_acknum_add_cnt(&g_client_tcp_man, 1);//acknum++
            TCP_LOG("client acknum to %u\n", g_client_tcp_man.acknum);

            /* Reply to ack, start the 2S timer, and finally delete it */
            tcp_build_ack_pkt(&fin_last_ackpkt, g_client_tcp_man.seqnum, g_client_tcp_man.acknum);
            tcp_send_pkt(fd, &fin_last_ackpkt, &servaddr, 0);

            tcp_timer_fd_init(&g_client_tcp_man.time_wait_fd, 2000, 0);
            tcp_epoll_add_fd(g_client_tcp_man.eop.epfd, g_client_tcp_man.time_wait_fd);
        } else {
            TCP_LOG("tcp state is TCP_FIN_WAIT2, recv pkt not we want seq pkt\n");
        }
        break;

    case TCP_TIME_WAIT:
        if (recvpkt->fin) {
            TCP_LOG("tcp state is TCP_TIME_WAIT, recv pkt have fin, send last ack\n");
            tcp_send_pkt(fd, &fin_last_ackpkt, &servaddr, 1);
        }
        break;

    default:
        break;
    }

    return 0;
}

void tcp_client_socket_epoll(struct epoll_event* events, int number)
{
	int i, s;
	int fd;
    uint64_t exp;

    /* Process fd event */
    for (i = 0; i < number; i++ ) {
        fd = events[i].data.fd;
        if (fd == client_sockfd) {
            if (events[i].events & EPOLLIN) {
                tcp_client_rcv_state_process(fd);
            } else {
                TCP_LOG("fd %d event not have EPOLLIN\n", fd);
            }
        } else if (fd == g_client_tcp_man.time_wait_fd){
            TCP_LOG("tcp state is %d, 2 ses timeout\n", g_client_tcp_man.state);
            if (g_client_tcp_man.state == TCP_TIME_WAIT) {
                TCP_LOG("tcp state is TCP_TIME_WAIT, 2 ses timeout\n");
                close(client_sockfd);
                exit(-1);//Process exit
            }
        } else {//Message expiration timer
            s = read(fd, &exp, sizeof(uint64_t));
            if (s != sizeof(uint64_t)) {
                TCP_LOG("read timetout fd %d error\n", fd);
            }
            tcp_client_tx_retry(fd, g_client_tcp_man.txqueue);
        }
    }

	return;
}

int main (int argc, char *argv[])
{
    struct in_addr servIP;
    unsigned short servPort;
    int ret;
    struct packet synpkt;

    if (argc != 4) {
        perror("ERROR: incorrect number of arguments\n");
        return -1;
    }

    if (inet_aton(argv[1], &servIP) == 0) {
        struct hostent* host_entry;
        host_entry = gethostbyname(argv[1]);
        if (host_entry == NULL) {
            perror("ERROR: IP address not in standard dot notation\n");
            return -1;
        }
        servIP = *((struct in_addr *) host_entry->h_addr_list[0]);
    }

    servPort = atoi(argv[2]);

    g_client_tcp_man.txqueue = tcp_queue_create(WND_SIZE);
    if (g_client_tcp_man.txqueue == NULL) {
        return -1;
    }

    fp = fopen(argv[3], "r");
    if (fp == NULL) {
        printf("ERROR: File %s not found\n", argv[3]);
        return -1;
    }

    if (tcp_epoll_init(&g_client_tcp_man.eop, 32) == -1) {
        return -1;
    }

    client_sockfd = socket(AF_INET, SOCK_DGRAM, 0);
	if (client_sockfd < 0){
		perror("socket error");
		return -1;
	}
    memset(&servaddr, 0x00, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr = servIP;
    servaddr.sin_port = htons(servPort);

    tcp_epoll_add_fd(g_client_tcp_man.eop.epfd, client_sockfd);

    /* Send SYN message */
    srand(time(NULL));
    g_client_tcp_man.seqnum = (rand() + 0x5678) % MAX_SEQN;
    g_client_tcp_man.acknum = 0;

    TCP_LOG("client init seqnum %u, acknum %u\n", g_client_tcp_man.seqnum,
        g_client_tcp_man.acknum);

    tcp_build_syn_pkt(&synpkt, g_client_tcp_man.seqnum);

    tcp_send_pkt(client_sockfd, &synpkt, &servaddr, 0);
    ret = tcp_queue_add_pkt(g_client_tcp_man.txqueue, &synpkt);
    if (ret == -1) {
        TCP_LOG("syn add queue pkt error\n");
        return -1;
    }
    tcp_epoll_add_fd( g_client_tcp_man.eop.epfd, ret);

    tcp_seqnum_add_cnt(&g_client_tcp_man, 1);//Send serial number +1
    TCP_LOG("client seqnum to %u\n", g_client_tcp_man.seqnum);

    g_client_tcp_man.state = TCP_SYN_SENT;

    /* Waiting for client to connect */
	while (1) {
        TCP_LOG("epoll wait......\n");
        ret = epoll_wait(g_client_tcp_man.eop.epfd, g_client_tcp_man.eop.events,
                    g_client_tcp_man.eop.nevents, -1);
        if (ret < 0 ) {
            perror( "epoll wait error\n" );
            break;
        }

        TCP_LOG("epoll wait return %d\n", ret);

        /* Process epoll events */
        tcp_client_socket_epoll(g_client_tcp_man.eop.events, ret);
    }

    return 0;
}
