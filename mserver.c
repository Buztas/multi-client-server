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
#include <signal.h>

#define N 20
#define HOST "127.0.0.1"
#define MAIL_PORT "8888"

typedef struct client_data {
  int sockfd;
  char username[256];
} client_data_t;

typedef struct ServerInfo {
  char name[100];
  char address[100];
  int port;
} ServerInfo;

int mail_service_sockfd;
ServerInfo serverInfo;
client_data_t *clients[N];
int client_count = 0;
pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;

void add_client(client_data_t *cl);
void client_left(char* username, char* message, int sender_sock);
void remove_client(int sockfd);
bool is_username_unique(const char* username);
void forwardMessageToServer(const char* serverName, const char* recipient, const char* message);
void handle_message(const char* received_msg);
void addServerToFile(const ServerInfo* server);
void broadcast_message(const char* message, int sender_sock);
int loadServersFromFile(ServerInfo* servers, int maxServers);
void removeServerFromFile(const char* serverName);
void* handle_client(void* sockfd);
int countLinesInFile(const char* filename);
void sigintHandler(int sig_num);

int main(int argc, char* argv[])
{
  
  struct addrinfo hints, *res;
  struct sockaddr_storage cli_addr;
  socklen_t cli_len;
  int status;
  int sockfd, newsockfd;


  if(argc != 2)
  {
    fprintf(stderr, "Bad CMD args");
    return 1;
  }
  //Connecting to mail service

  int numbytes;
  char buf[1024];
  struct addrinfo cints, *servinfo, *p;
  int rv;
  char s[INET6_ADDRSTRLEN];

  memset(&cints, 0, sizeof(cints));
  cints.ai_family = AF_UNSPEC;
  cints.ai_socktype = SOCK_STREAM;

  signal(SIGINT, sigintHandler);

  // Use HOST and MAIL_PORT as strings
  if((rv = getaddrinfo(HOST, MAIL_PORT, &cints, &servinfo)) != 0) {
      fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
      return 1;
  }
  for(p = servinfo; p != NULL; p=p->ai_next) {
      if((mail_service_sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
          perror("mail service: socket");
          continue;
      }

      if(connect(mail_service_sockfd, p->ai_addr, p->ai_addrlen) == -1) {
        close(mail_service_sockfd);
        perror("mail service: connect");
        continue;
      }
      break;
  }

  if(p == NULL) {
      fprintf(stderr, "mail service: failed to connect\n");
      return -2;
  }

  freeaddrinfo(servinfo);

  //Server setup
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET6;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE;
  
  if((status = getaddrinfo(NULL, argv[1], &hints, &res)) != 0)
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

  //somewhat of DNS server to store server names
  int lineCount = countLinesInFile("server_config.txt");
  sprintf(serverInfo.name, "Server%d", lineCount);
  inet_ntop(AF_INET, res->ai_addr, serverInfo.address, INET6_ADDRSTRLEN);
  serverInfo.port = atoi(argv[1]);
  addServerToFile(&serverInfo);


  fprintf(stdout, "Server name: %s\nServer address: %s\nServer port: %d\n", serverInfo.name, serverInfo.address, serverInfo.port);
  fflush(stdout);
  
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

void sigintHandler(int sig_num) {
  (void) sig_num;

  removeServerFromFile(serverInfo.name);

  exit(0);
}

int countLinesInFile(const char* filename) {
  FILE *file = fopen(filename, "r");
  if(!file) {
    perror("Unable to open the file");
    return -1;
  }

  int lines = 0;
  int ch;
  while(EOF != (ch = getc(file))) {
    if (ch == '\n')
      lines++;
  }

  fclose(file);
  return lines;
}


void broadcast_message(const char* message, int sender_sock)
{
  pthread_mutex_lock(&clients_mutex);
  for(int i = 0; i < client_count; i++)
  { 
      send(clients[i]->sockfd, message, strlen(message), 0);
  }
  pthread_mutex_unlock(&clients_mutex);
}

void add_client(client_data_t *cl)
{
  pthread_mutex_lock(&clients_mutex);
  clients[client_count] = cl;
  client_count++;
  pthread_mutex_unlock(&clients_mutex);
}

void client_left(char* username, char* message, int sender_sock)
{
  pthread_mutex_lock(&clients_mutex);
  for(int i = 0; i < client_count; i++)
  {
    send(clients[i]->sockfd, strncpy(username, message, sizeof(username) + 30), sizeof(username)
         +sizeof(message), 0);
  }
  pthread_mutex_unlock(&clients_mutex);
}

void remove_client(int sockfd)
{
  pthread_mutex_lock(&clients_mutex);
  for(int i = 0; i < client_count; i++)
  {
    if(clients[i]->sockfd == sockfd)
    {
      clients[i] = clients[client_count -1];
      client_count--;
      break;
    }
  }
  pthread_mutex_unlock(&clients_mutex);
}

bool is_username_unique(const char* username) 
{
  pthread_mutex_lock(&clients_mutex);
  for(int i = 0; i < client_count; i++)
  {
    if(strcmp(clients[i]->username, username) == 0)
    {
      pthread_mutex_unlock(&clients_mutex);
      return false;
    }
  }
  pthread_mutex_unlock(&clients_mutex);
  return true;
}


void handle_message(const char* received_msg) {
  // char recipient[256], serverName[256], actualMessage[1024];

  // sscanf(message, "%255[^@]@%255s %1023[^\n]", recipient, serverName, actualMessage);

  // char serverAddress[100];
  // int serverPort;

}


void addServerToFile(const ServerInfo* server) {
    FILE* file = fopen("server_config.txt", "a"); // Append mode
    if (file == NULL) {
        perror("Failed to open server config file");
        return;
    }
    fprintf(file, "%s,%s,%d\n", server->name, server->address, server->port);
    fclose(file);
}

int loadServersFromFile(ServerInfo* servers, int maxServers) {
    FILE* file = fopen("server_config.txt", "r");
    if (file == NULL) {
        perror("Failed to open server config file");
        return 0; // No servers loaded
    }

    int count = 0;
    while (fscanf(file, "%99[^,],%99[^,],%d\n", servers[count].name, servers[count].address, &servers[count].port) == 3) {
        count++;
        if (count >= maxServers) break;
    }
    fclose(file);
    return count; // Number of servers loaded
}

void removeServerFromFile(const char* serverName) {
    ServerInfo servers[100]; // Adjust size as needed
    int count = loadServersFromFile(servers, 100);

    FILE* file = fopen("server_config.txt", "w"); // Open in write mode to clear and rewrite
    if (file == NULL) {
        perror("Failed to open server config file for updating");
        return;
    }

    for (int i = 0; i < count; i++) {
        if (strcmp(servers[i].name, serverName) != 0) {
            fprintf(file, "%s,%s,%d\n", servers[i].name, servers[i].address, servers[i].port);
        }
    }
    fclose(file);
}

void* handle_client(void* sockfd)
{
  printf("Connection established, thread started \n");
  client_data_t* data = (client_data_t*) sockfd;
  int newsockfd = data->sockfd; 
  char buffer[1024];
  char username[256];
  char message[0 + 1024 + 256 + 12 + 2000];
  int bytes_read;
  char mail_address[1024];
  
  send(newsockfd, "ATSIUSKVARDA\n", strlen("ATSIUSKVARDA\n"), 0);
  bytes_read = recv(newsockfd, username, sizeof username, 0);

  if(bytes_read <= 0)
  {
    perror("recv for username");
    close(newsockfd);
    free(data);
    return NULL;
  }

  username[bytes_read-2]='\0';
  strcpy(data->username, username);



  if(is_username_unique(username) == false)
  {
    printf("User already exists. \n");
    return NULL;
  } else {
    add_client(data);
  }

  snprintf(mail_address, sizeof(mail_address), "%s@%s", username, serverInfo.name);
  fprintf(stdout, "mail address: %s", mail_address);
  fflush(stdout);

  while(1)
  {
    bytes_read = recv(newsockfd, buffer, sizeof(buffer) -1, 0);
    buffer[bytes_read]='\0';
    if(bytes_read <= 0)
    {
      if(bytes_read == 0)
      {
         printf("Client %s disconnected", username);
      } else {
        perror("recv_failed");
       }
       close(newsockfd);
       remove_client(newsockfd);
       return NULL;
    } else {

    char fullMessage[256 + 2024];  

    if(strncmp(buffer, "SEND", 4) == 0)
    {

      snprintf(fullMessage, sizeof(fullMessage), "%s|%s", buffer, mail_address);

      int bytes_sent = send(mail_service_sockfd, fullMessage, strlen(fullMessage), 0);

      if(bytes_sent == -1)
      {
        perror("send to mail");
      }

    } else if(strncmp(buffer, "#?", 2) == 0)
    {
      char filename[300 + 300];
      snprintf(filename, sizeof(filename), "%s@%s_messages.txt", username, serverInfo.name);
      
      fprintf(stdout, "\nfilename: %s\n", filename);
      fflush(stdout);

      FILE *file = fopen(filename, "r");
      if (file) {
          char line[1024];
          // Read each line of the file and send it to the client
          while (fgets(line, sizeof(line), file) != NULL) {
              send(data->sockfd, line, strlen(line), 0);
          }
          // Indicate the end of messages
          send(data->sockfd, "END_OF_MESSAGES\n", 17, 0);
          fclose(file);
      } else {
          // If the file couldn't be opened, maybe the user has no messages
          send(data->sockfd, "No messages found.\n", 19, 0);
      }
    }
    else {
      snprintf(message, sizeof(message), "%s : %s", username, buffer);

      broadcast_message(message, newsockfd);
    }

    }

  }

  remove_client(newsockfd);
  close(newsockfd);
  free(data);
  return NULL;
}



