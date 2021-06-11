Name: Rick
UID: 
Email: 

## The High Level Description:

I desgin the project based on the given skeleton code. I extractd the common parts of function that server and client both will use and put in a header file `common.h`, which improves the visibility. And I could add the common implementions in it such as setting up TCP state. 

Basic logic follow the spec: 

The goal of this project is to use UDP to build a reliable delivery similar as TCP. The process:

- The server creates a UDP socket and waits for datagrams from the client.

- First, the client open a UDP socket and initiate 3-way handshake to specified IP and port. 

  - It send UDP packet no payload and 12 byte heeader with the SYN flag set, initially the ACKNum is set to 0, the SeqNum is a random number not exceeding 25600. 
    - The server receive the message, parse the body, write it to a file, then send back with SYN and ACK flag, acknowledging that the conncection is established. (ACKNum = SeqNum+1)

- Data transfer: The program handles loss recovery with Selective Repeat, the sender keeps a timer for each unacked packets.  A receiver buffer is created in the server side. Receive individually acknoledges all correctly received packets. The client only resends packets for which ACK not received, (i.e., the timer for this packets will time out). If a timer times out, the corresponding (lost) packet would be retransmitted for the successful file transmission. In my program, I use timerfd library and operate the timer via FD.

  The packet size that client sent is based on MSS, here is 524 bytes (header 12 bytes and maximum sie of 512 bytes for payload). The packets are save in a buffer so that if they were timeout, client can retransmit them. In the server side, need to check if the packets are in-order. i.e., check if the ACKnum are as expected. 

   In the data delivery, the initial window size is 10, first the client transmits 10 packets, and then increments the window by 1 after it receives an ACK from the first packet. This operation continue until the file is send to the server entirely.

- All files transfered (all bytes acknowledged), client terminates the connection:

  - Send UDP packet with FIN
  - the server receive the msg, send back an ACK followed by FIN. 
  - Respond to each incoming FIN with an ACK packet, drop any non-FIN packet
  - After recive the FIN, the client wait for 2 secs to close connection. 

## Problems Have Met

1. The main problem I met is reasoning about seq number. Bug happened due to improperly incrementing or storing seq numbers. 
2. The order of saving file is not correct. The receiver need to compare the received ACK, if it not as expected, need to send back an ACK to tell opposite side to resend. 
3. How to test the program is kind of confused, and sometimes there is infinite loop problem when test the loss recovery.
4. To challenge myself, I use timerfd and Epoll library, they are kind of complicated, and need to do a lot research to get familiar with them. And have to admit that they are important and useful in socket programming. 

## Reference

- piazza@269
- https://sites.uclouvain.be/SystInfo/usr/include
- https://www.geeksforgeeks.org/udp-server-client-implementation-c/
- https://www.geeksforgeeks.org/tcp-server-client-implementation-in-c/
- https://users.cs.northwestern.edu/~agupta/cs340/project2/TCPIP_State_Transition_Diagram.pdf
- https://en.wikipedia.org/wiki/Reliable_User_Datagram_Protocol
- https://sites.uclouvain.be/SystInfo/usr/include/sys/epoll.h.html
- https://blog.csdn.net/armlinuxww/article/details/92803381
- https://man7.org/linux/man-pages/man2/timerfd_create.2.html
- https://man7.org/linux/man-pages/man7/epoll.7.html

## Testing Environment

Ubuntu 20.04 (Virtual Machine)

## Some Updates after Demo
6/10/2021: 
The program when sending 1MB file with 20% loss rate is extremely slow. I may implement SR in a wrong way. Will check on it when I am available. 

6/11/2021:
Reason found, the receiver buffer didn't implement correctly, it doesn't handle out-of-order packet. Will update the code later.
