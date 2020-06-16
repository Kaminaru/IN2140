#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <dirent.h>
#include <sys/stat.h>

#include "pgmread.h"
#include "send_packet.h"

// A linked list that holds data, name and dataSize of the file, and also points at next Node
struct Node {
    char* bufData;
    struct Node* next;
    char* name;
    int dataSize;
};

void error(int ret, char *msg) {
    if (ret == -1) {
        perror(msg);
        exit(EXIT_FAILURE);
    }
}

// I decided at the start of server program save all files from dir to buffers
// so it will take less time for server to compare each image from client with all
// the images on the server
struct Node* setUpFileBuffers(char* directory_name){
  // unsigned char typeNeeded = 8; // format type for .pgm (if(sd->d_type == typeNeeded))
  struct Node* head = NULL; // head of lenkedlist
  struct dirent* sd; // directory struct
  int dirNameSize = strlen(directory_name);
  DIR *dir = opendir(directory_name);
  if(dir == NULL){
    printf("Error! No such directory\n");
    exit(EXIT_FAILURE);
  }

  struct stat file_info;
  struct Node* lastAdded = NULL;
  while((sd = readdir(dir)) != NULL){
    int nameL = strlen(sd->d_name);

    char path[nameL+dirNameSize+2]; // +1 for nulltermination, +1 for '/'
    memcpy(&path, directory_name, dirNameSize); // |b|i|g|_|s|e|t||....||
    memcpy(&path[dirNameSize], "/", 1);         // |b|i|g|_|s|e|t|/||....||
    memcpy(&path[dirNameSize+1], sd->d_name, nameL); // |b|i|g|_|s|e|t|name|
    memcpy(&path[dirNameSize+1+nameL], "\0", 1); // adds nulltermination

    lstat(path, &file_info);
    if(S_ISREG(file_info.st_mode)){ // if this is a file
      if(head == NULL){ // no elements in lenkedlist
        lastAdded = (struct Node*)malloc(sizeof(struct Node));
        head = lastAdded;
      }
      else{
        lastAdded->next = (struct Node*)malloc(sizeof(struct Node));
        lastAdded = lastAdded->next;
      }
      lastAdded->next = NULL;
      // NAME
      lastAdded->name = malloc(strlen(sd->d_name)+1); // +1 for nulltermination
      strcpy(lastAdded->name, sd->d_name);
      // DATA SIZE (FILE size)
      lastAdded->dataSize = file_info.st_size; // in bytes
      // DATA
      FILE* fp = fopen(path, "r"); // using the path i made before
      if(fp != NULL){
        // Allocating buffer
        char* fileData = malloc(sizeof(unsigned char) * file_info.st_size);
        // Copy whole file into memory
        fread(fileData, sizeof(unsigned char), file_info.st_size, fp);
        lastAdded->bufData = fileData;
      }
      fclose(fp);
    }
  }
  closedir(dir);
  return head;
}

// Writes output to the file with given name (from arguments)
int writeToFile(unsigned char* payloadName, char* serverName, char* outfile_name){
  FILE *fileWrite = fopen(outfile_name, "a+"); // a+ open for 'reading and writing' (while w opens it for writing freshly).
  if(fileWrite == NULL){
    printf("Couldn't make a file in writeToFile\n");
    return 0;
  }

  fprintf(fileWrite,"<%s> <%s>\n", payloadName,serverName);

  // closing third file
  if(fclose(fileWrite) != 0){
    printf("File is not close properly, in packageCheck\n");
    return 0;
  }
  return 1;
}

// Four chars from buffer becomes int
int fourCharToOneInt(unsigned char* buf){
  return (buf[0] << 24) | (buf[1] << 16) | (buf[2] << 8) | buf[3];
}

// checks package for corruption; Checking with all the existing files
int packageCheck(unsigned char* buf, char* outfile_name, struct Node* head){
  int image_from_client_size = 0; // saves the size of image from client
  unsigned int pacLength = fourCharToOneInt(buf);
  buf = buf+12; // extra + 4 for next 4 char that function won't use; +4 to skip uniqNum
  int fileNameSize = fourCharToOneInt(buf); // with null termination
  buf = buf+4;
  image_from_client_size = image_from_client_size + 16;
  unsigned char* filename = malloc(fileNameSize);
  for(int i = 0; i < fileNameSize; i++){
    filename[i] = buf[i];
  }
  buf = buf + fileNameSize;
  image_from_client_size = pacLength - (image_from_client_size + fileNameSize);

  // |buf| is now at position of first byte for the file from client
  struct Image* img_from_client = Image_create((char*)buf); // making Image struct
  struct Image* img_from_server;
  int ret;
  char* tempBuf;

  struct Node* tmpN = head;
  while(tmpN != NULL){ // goes throw linked list
    if(image_from_client_size == tmpN->dataSize){    // goes in ONLY if Data size of image from server
      tempBuf = malloc(tmpN->dataSize);             // and image from client have the same size
                                                   // (no point to check if they are same when they have different size)
      memcpy(tempBuf, tmpN->bufData, tmpN->dataSize); // copy data, because Image_create changes
      img_from_server = Image_create(tempBuf);       // buffer (and i need to save originall for later use)

      // compares image from client and image from server
      if(Image_compare(img_from_client,img_from_server)){ // if identical
        free(tempBuf);
        Image_free(img_from_server);
        Image_free(img_from_client);
        ret = writeToFile(filename, tmpN->name, outfile_name);
        free(filename);
        return ret;
      }
      Image_free(img_from_server);
      free(tempBuf);
    }
    tmpN = tmpN->next;
  }
  // If didn't found identical file
  char* unknown = "UNKNOWN";
  ret = writeToFile(filename, unknown, outfile_name);
  Image_free(img_from_client);
  free(filename);
  return ret;
  return 0;
}

int main(int argc, char *argv[]) {
    set_loss_probability(0.2f); // from 0 to 1


    if(argc < 3){
      printf("usage: ./upserver PortNumber imageListFile outputFile\n");
      return 0;
    }

    char *directory_name, *outfile_name;
    unsigned short my_port;
    struct sockaddr_in my_addr, si_clinet;
    struct in_addr ip_addr;
    int so, rc;
    socklen_t siclen = sizeof(si_clinet);
    unsigned char exp_seq_num; // the same as lower_bund
                                // goes up after server adds package with expected seq. num. in the "work list"

    my_port = atoi(argv[1]);
    directory_name = argv[2];
    outfile_name = argv[3];
    exp_seq_num = 0;

    struct Node* head = setUpFileBuffers(directory_name);; // head of linked list


    // Will listen for IPv4-adress for localhost
    inet_pton(AF_INET, "127.0.0.1", &ip_addr);

    // Will listen with IPv4 with given port number
    memset((char *) &my_addr, 0, sizeof(my_addr)); // puts 0 to whole struct; Because it will have an arbitrary value upon creation
    my_addr.sin_family = AF_INET;
    my_addr.sin_port = htons(my_port);
    my_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    // socket that revieves datagramms with IPv4 */
    so = socket(AF_INET, SOCK_DGRAM, 0);
    error(so, "socket");

    // socket will listen given adress
    rc = bind(so, (struct sockaddr *)&my_addr, sizeof(struct sockaddr_in));
    error(rc, "bind");

    unsigned char* buf = malloc(1490); // client must send not more than 1450 byte of data
    while(1){
      // wait until anything comes
      rc = recvfrom(so, buf, 1490, 0, (struct sockaddr*) &si_clinet, &siclen); // stops here and waits for message
      error(rc, "recvfrom");

      if(buf[6] == 0x04){ // If this is a termination packet
        printf("\nServer is terminated by client (%s:%d) request\n\n", inet_ntoa(si_clinet.sin_addr), ntohs(si_clinet.sin_port));
        break; // goes out of while loop, to free rest of the allocated memory before closing program
      }else if(buf[6] == 0x01){ // If packet with payload
        // If server got package with expected sequence number
        if(buf[4] == exp_seq_num){
          if(packageCheck(buf, outfile_name, head)){ // if package is error free
            printf("\nGOT WHAT I NEED! Sending ACK:%d to client!\n", exp_seq_num);
            char* ack = malloc(8);
            for(int i = 0; i < 8; i++){
              if(i == 4){ // client cares only about ack[4]
                ack[i] = exp_seq_num;
              }else{ // so rest 7 bytes will be 0
                ack[i] = 0;
              }
            }
            exp_seq_num++;
            rc = send_packet(so, ack, 8, 0, (struct sockaddr*)&si_clinet, sizeof(struct sockaddr_in));
            error(rc, "send_packet");
            free(ack);
          }else{//package is corrupted = do nothing and wait for timeout
            printf("Package is corrupted\n");
          }
        }else if(buf[4] < exp_seq_num){ // if client have send packaet that server already have send ACK for (ACK got lost before)
          // so if exp_seq_num = 3 and buf[4] = 1, that means that we lost ACK for packet 1 and 2
          // so server can send ACK for 1 and 2 again.
          printf("\n");
          for(int i = buf[4]; i < exp_seq_num; i++){
            printf("ACK has been lost. Sending ACK:%d to client again!\n", i);
            char* ack = malloc(8);
            for(int k = 0; k < 8; k++){
              if(k == 4){
                ack[k] = i;
              }else{
                ack[k] = 0;
              }
            }

            rc = send_packet(so, ack, 8, 0, (struct sockaddr*)&si_clinet, sizeof(struct sockaddr_in));
            error(rc, "send_packet");
            free(ack);
          }
        }
      }
    }
    free(buf);
    close(so);

    // frees malloc for every element
    struct Node* tmpN = head;
    while(tmpN != NULL){
      free(tmpN->name);
      free(tmpN->bufData);
      struct Node* delete = tmpN;
      tmpN = tmpN->next;
      free(delete);
    }

    return EXIT_SUCCESS;
}
