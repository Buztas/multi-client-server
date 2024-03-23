#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <errno.h>
#include <unistd.h>
#include <stdbool.h>

#define N 20
#define HOST "127.0.0.1"
#define PORT "8888"

typedef struct client_data {
  int sockfd;
  char username[256];
} client_data_t;

client_data_t *clients[N];
int client_count = 0;
pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;


void* handle_client(void* sockfd)
{
    fprintf(stdout, "Mail service successfully connected to server\n");
    fflush(stdout);
    client_data_t* data = (client_data_t*) sockfd;
    int newsockfd = data->sockfd;
    char buffer[1024];

    size_t bytes_received;
    while(1)
    {
      bytes_received = recv(newsockfd, buffer, sizeof buffer, 0);
      if(bytes_received <= 0) {
          perror("sockfd");
          free(data);
          close(newsockfd);
          return NULL; // Ensure we exit the thread function after cleanup.
      }
      buffer[bytes_received] = '\0';
    
    char* command = strtok(buffer, "|");
    char* mail_address = strtok(NULL, "");
    if(strncmp(command, "SEND", 4) == 0) {
    // Move past "SEND " to the start of the recipient@server part
    char *content = command + 5; // Assuming a space after SEND

    char *space = strchr(content, ' ');
    if (space != NULL) {        
        *space = '\0';
        space++;

        char *fileNaming= strtok(content, " ");
        char *recipient = strtok(content, "@");
        char *server = strtok(NULL, "");

        char filename[300];
        snprintf(filename, sizeof(filename), "%s@%s_messages.txt", recipient, server);
        
        // fprintf(stdout, "full filename: %s\nfull recipient: %s\nfull server: %s\n", filename, recipient, server);
        // fflush(stdout);
        FILE *file = fopen(filename, "a");
        if(file) {
              fprintf(file, "%s : %s", mail_address, space);
              fclose(file);
        } else {
            perror("Failed to open file");
        }
        }
      } 
    }




    close(newsockfd);
    free(data);
    return NULL;
}



int main(void)
{
    //TODO
    //Create server socket, bind it, listen and then in the while loop accept
    //in while loop use pthreading to accept client connections
    //if all is good use mutex's to check unique usernames
  
  struct addrinfo hints, *res, *p;
  struct sockaddr_storage cli_addr;
  socklen_t cli_len;
  int status;
  int sockfd, newsockfd;

  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET6;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE;
  
  if((status = getaddrinfo(NULL, PORT, &hints, &res)) != 0)
  {
    fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(status));
    return 2;
  }
  
  if((sockfd=socket(res->ai_family, res->ai_socktype, res->ai_protocol)) == -1)
  {
    perror("socket");
    return 3;
  }

  int no = 0;
  if(setsockopt(sockfd, IPPROTO_IPV6, IPV6_V6ONLY, (void *)&no, sizeof(no)) == -1)
  {
    perror("setsockopt");
    exit(1);
    return 1;
  }

  if((bind(sockfd, res->ai_addr, res->ai_addrlen)) == -1)
  {
    perror("bind");
    return 4;
  }

  if(listen(sockfd, N)!=0)
  {
    perror("listen");
    return 5;
  }


  while(1)
  {
    cli_len = sizeof cli_addr;
    newsockfd = accept(sockfd, (struct sockaddr*) &cli_addr, &cli_len);
    
    if(newsockfd == -1)
    {
      perror("accept");
      continue;
    }
    client_data_t* client_data = malloc(sizeof(client_data_t));
    if(client_data == NULL)
    {
      perror("malloc");
      close(newsockfd);
      continue;
    }
    
    client_data->sockfd = newsockfd;

    pthread_t thread;
    if(pthread_create(&thread, NULL, handle_client, (void*)client_data) != 0)
    {
      perror("pthread_create");
      close(newsockfd);
      free(client_data);
      continue;
    }   
    //client_data_t *client_data = malloc(sizeof(client_data_t))
    //
    pthread_detach(thread);
    
  }

    return 0;
}
