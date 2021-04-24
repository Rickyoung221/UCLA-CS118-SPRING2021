#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define MYPORT 8081 /* Avoid reserved ports */
#define BACKLOG 10 /* pending connections queue size */
#define BUFSIZE 8096


struct MIME{
    char *ext;
    char *filetype;
} ;
// The minimum supported file types
struct MIME mime [] = {
    {"gif", "image/gif" },
    {"jpg", "image/jpeg"},
    {"jpeg","image/jpeg"},
    {"txt",  "text/plain"},
    {"htm", "text/html" },
    {"html","text/html" },
    {0,0} };
void server(int fd);
//the buffer save the client's request
char buffer[BUFSIZE+1];
//server function


int main(int argc, char *argv[])
{
    int listenfd, socketfd;
    struct sockaddr_in serv_addr;
    struct sockaddr_in cli_addr;
    
    //check the arguments
    if( argc != 3) {
        fprintf(stderr,"usage: server [port] [document directory]\n");
        exit(0);
    }
    //change to the document directory
    if(chdir(argv[2]) == -1){
        fprintf(stderr, "can't change to document directory %s\n",argv[2]);
        exit(1);
    }

    //we do not care the dead child process
    signal(SIGCLD, SIG_IGN);
    signal(SIGHUP, SIG_IGN);
     
    //print the welcome msg.
    fprintf(stdout,"http server starting\n");
    
    //create a socket.
    if((listenfd = socket(AF_INET, SOCK_STREAM,0)) <0){
        perror("socket");
        exit(1);
    }
    //reuse the port num
    if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int)) < 0){
        perror("setsockopt(SO_REUSEADDR) failed");
        exit(1);
    }
    
    //get the port number
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(atoi(argv[1]));
    
    //bind the sock
    if(bind(listenfd, (struct sockaddr *)&serv_addr,sizeof(serv_addr)) <0){
        perror("bind");
        exit(1);
    }
    //listen
    if( listen(listenfd,12) <0){
        perror("listen");
        exit(1);
    }
    //while loop server
    while(1) {
        size_t length = sizeof(cli_addr);
        if((socketfd = accept(listenfd, (struct sockaddr *)&cli_addr, (socklen_t*)&length)) < 0){
            perror("accept");
            exit(1);
        }
        //fork a new client.
        pid_t pid = fork();
        if(pid < 0) {
            perror("fork");
            exit(1);
        } else {
            if(pid > 0) {
                close(socketfd);
            } else {
                close(listenfd);
                //do server.
                server(socketfd);
                
            }
        }
    }
}

void server(int fd){
    
    int i, request_len;
    char *file_type = "application/octet-stream";
    
    memset(buffer,'\0',BUFSIZE+1);
    request_len =read(fd,buffer,BUFSIZE);
    //if we read at some error.
    if(request_len <= 0) {
        fprintf(stderr,"READ ERROR! \n");
        exit(1);
    }
    //clean the new line
    for(i=0;i<request_len;i++)    {
        if(buffer[i] == '\r' || buffer[i] == '\n')
            buffer[i]='*';
    }
    //we only support HTTP GET method.
    if( strncasecmp(buffer,"GET ",4)){
        write(fd, "HTTP/1.1 500 Internal Server Error\nContent-Length: 148\nConnection: close\nContent-Type: text/html\n\n<html><head>\n<title>500 Internal Server Error</title>\n</head><body>\n<h1>Internal Server Error</h1>\nThis server is not working.\n</body></html>\n",239);
    
    }

    for(i=4;i<BUFSIZE;i++) {
        if(buffer[i] == ' ') {
            buffer[i] = '\0';
            break;
        }
    }

    //if we are look for the parent directory
    if(strstr(buffer, "GET /..")){
            write(fd, "HTTP/1.1 403 Forbidden\nContent-Length: 191\nConnection: close\nContent-Type: text/html\n\n",91);
            write(fd, "<html><head>\n<title>403 Forbidden</title>\n</head><body>\n<h1>Forbidden</h1>\nThe requested URL, file type or operation is not allowed on this simple static file webserver.\n</body></html>\n",191);
    
    }
    //if we are look for the index.html
    if( strncasecmp(&buffer[0],"GET /\0",6) == 0 ) {
        strcpy(buffer,"GET /index.html");
    }
 
    //check supported mime
    for(i=0;mime[i].ext != 0;i++) {
        int len = strlen(mime[i].ext);
        if( strncmp(&buffer[strlen(buffer)-len], mime[i].ext, len) == 0) {
            file_type = mime[i].filetype;
            break;
        }
    }
    //the request file name.
    char *file_name = &buffer[5];
    char clean_name[BUFSIZE] = {0};
    //printf("clean_name:%s\n",file_name);
    char *p = strstr(file_name, "%20");
    if(!p)
        strcpy(clean_name,file_name);
    if(p){
        strncat(clean_name, file_name,p-file_name);
    }
    while(p){
        
        strcat(clean_name, " ");
        p+=3;
        char *tp = p;
        p = strstr(p, "%20");
        if(!p){
            strcat(clean_name,tp);
            break;
        }
    };
    int file_fd;
    //If cannot find the requestes file, return 404
    if(( file_fd = open(clean_name, O_RDONLY)) == -1) {
        write(fd, "HTTP/1.1 404 Not Found\nContent-Length: 146\nConnection: close\nContent-Type: text/html\n\n",91);
        write(fd, "<html><head>\n<title>404 Not Found</title>\n</head><body>\n<h1>Not Found</h1>\nSorry,The requested URL was not found on this server.\n</body></html>\n",150);
    }

    //blank line after last line
    sprintf(buffer,"HTTP/1.0 200 OK\r\nContent-Type: %s\r\n\r\n", file_type);
    write(fd,buffer,strlen(buffer));
    int result_len = 0;
    //read the file.
    while (    (result_len = read(file_fd, buffer, BUFSIZE)) > 0 ) {
        //send the file.
        write(fd,buffer,result_len);
    }

    exit(0);
}
