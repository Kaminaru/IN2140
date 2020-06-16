#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <time.h>
#include <libgen.h>

#include "send_packet.h"


// A linked list that holds data, name and dataSize of the Image, and also points at next Node
struct Node {
    char* bufData;
    struct Node* next;
    char* name;
    int dataSize;
};

char** fileNamePointers; // Global allocated memory for pointer to every filename string
int* packetSize; // Global allocated memory to hold temporary size of packet
int* number_of_files; // Global allocated memory for number of files

void error(int ret, char* msg) {
    if (ret == -1) {
        perror(msg);
        exit(EXIT_FAILURE);
    }
}

// Check for "read file" existence and all the files that was mentioned in the "read file"
// If file exit, function will add buffer with name to fileNamePointers array
void checkForFileExistence(char* name){

  // checks if file exist
  FILE* fileptr = fopen(name, "r"); // file pointer; "rb" for binary files, "r" for normal files
  if(fileptr == NULL){
    printf("There is no such file as %s\n", name);
    exit(EXIT_FAILURE);
  }

  // finds number of files in txt file
  number_of_files = malloc(sizeof(int*));
  number_of_files[0] = 0;
  char chr = getc(fileptr); // extract character from file and store in chr
  while (chr != EOF){
    //Count whenever new line is encountered
    if (chr == '\n'){
      number_of_files[0] = number_of_files[0] + 1;
    }
    //take next character from file
    chr = getc(fileptr);
  }

  fseek(fileptr, 0, SEEK_SET); // comes to start of the file

  char filename[256];
  int i = 0;
  fileNamePointers = malloc(number_of_files[0]*sizeof(char*));
  // checks if files in "read file" exist in folder
  while(fscanf(fileptr, "%[^\n]\n", filename) != -1){ // read line
    if(access(filename, F_OK) == -1) {
      printf("File: %s does not exist", filename);
      printf(" (Check if file: %s right) \n", name);
      exit(EXIT_FAILURE);
    }
    char* fileNameBuff = malloc(sizeof(filename));
    if(fileNameBuff == NULL){
      printf("Couldn't allocate memory for fileNameBuff\n");
      exit(EXIT_FAILURE);
    }
    strcpy(fileNameBuff,filename);
    // add pointer for string in fileNamePointers array
    fileNamePointers[i] = fileNameBuff;
    i++;
  }

  if(fclose(fileptr) != 0){
    printf("File is not closed properly\n");
    exit(EXIT_FAILURE);
  }
}


// Write Big-Endian int value into buffer; assuming that we use 32 bit system
char* serialize_int(int value, char* buffer){
  buffer[0] = value >> 24;
  buffer[1] = value >> 16;
  buffer[2] = value >> 8;
  buffer[3] = value;
  return buffer + 4; // change position of buffer pointer;
}

// Write String to a buffer
char* serialize_string(char* string, char* buffer){
  for(size_t i = 0; i < strlen(string); i++){
    buffer[i] = string[i];
  }
  return buffer + strlen(string);
}

// Adds given char to a buffer
char* serialize_char(unsigned char ch, char* buffer){
  buffer[0] = ch;
  return buffer + 1;
}

// Copy all the file bytes to a buffer
char* serialize_file(int size, char* filename, char* buffer){
  FILE* fp = fopen(filename, "r");
  if (fp == NULL){
    printf("\nFile open in serialize_file failed! |%s|\n", filename);
    exit(EXIT_FAILURE);
  }

  for(int i = 0; i < size; i++) {
      buffer[i] = fgetc(fp); // reads each character and adds to the buffer |i| position
  }

  if(fclose(fp) != 0){
    printf("File is not closed properly at serialize_file\n");
    exit(EXIT_FAILURE);
  }
  return buffer + size; // change position of buffer point (don't really need it because file is the last element in buffer anyways)
}

// Builds package bytes in the buffer and returns it (serialization of data)
char* createpackage(char* name, unsigned char sequenceNum, unsigned char flag){
  int packSize, filesize, payloadSize;
  char* buffer, * bufferStartPointer;

  // finds size of payload
  FILE* fp = fopen(name, "r");
  if (fp == NULL) {
    printf("File Not Found at createPackage! |%s|\n", name);
    exit(EXIT_FAILURE);
  }

  fseek(fp, 0L, SEEK_END); // comes to the end of the file
  filesize = ftell(fp); // gets size of file

  if(fclose(fp) != 0){
    printf("File is not closed properly in createpackage. |%s|\n", name);
    exit(EXIT_FAILURE);
  }

  if(flag == 0x01){ // packet with payload
    // calculating the payloadSize
    payloadSize = filesize + 9 + strlen(basename(name)); // 4 + 4 + 1 ('\0') bytes for uniq num, filename length;
    packSize = 8 + payloadSize; // 8 bytes for header, rest is ofr payload
    packetSize[0] = packSize; // changing value of global variable, so i can use it in creating of linked list node in main method
    buffer = malloc(packSize);
    bufferStartPointer = buffer; // points on start of buffer, because i change pointer on "buffer" a lot, so i need to save start position
    // ----------------PACKAGE HEADER:-----------------
    buffer = serialize_int(packSize, buffer); // first four bytes for int in header:
    buffer = serialize_char(sequenceNum, buffer);
    buffer = serialize_char(0, buffer); // client doesn't use this line of package so we set it to 0
    buffer = serialize_char(flag, buffer);
    buffer = serialize_char(0x7f, buffer);
    // ----------------PACKAGE PAYLOAD:----------------
    buffer = serialize_int(888, buffer); // unique number of the request
    buffer = serialize_int(strlen(basename(name)) + 1, buffer); // file size name with '\0'
    buffer = serialize_string(basename(name), buffer); // filename
    buffer = serialize_char('\0', buffer); // nulltermination for filename
    buffer = serialize_file(filesize, name, buffer); // files bytes
    return bufferStartPointer;

  }else if(flag == 0x04){ // termination packet
    buffer = malloc(8);
    bufferStartPointer = buffer;
    // ----------------PACKAGE HEADER:-----------------
    buffer = serialize_int(9, buffer); // first four bytes for int in header:
    buffer = serialize_char(sequenceNum, buffer);
    buffer = serialize_char(0, buffer); // client doesn't use this line of package so we set it to 0
    buffer = serialize_char(flag, buffer);
    buffer = serialize_char(0x7f, buffer);
    return bufferStartPointer;
  }
  return NULL;
}

// Creates and adds given number of packets in linked list
struct Node* addPackagetoList(int numberOfP, unsigned char sequenceNum, struct Node* lastAdded){
  for(int i = 0; i < numberOfP; i++){ // goes for number of nodes program want to add to linked list
    lastAdded->next = (struct Node*)malloc(sizeof(struct Node)); // old last added points now on newly added
    lastAdded = lastAdded->next; // now last added is truly last added;
    lastAdded->bufData = createpackage(fileNamePointers[(int)sequenceNum], sequenceNum, 0x01);
    lastAdded->next = NULL;
    lastAdded->name = basename(fileNamePointers[(int)sequenceNum]);
    lastAdded->dataSize = packetSize[0];
    sequenceNum++;
  }
  return lastAdded; // Changes lastAdded struck Node variable in main method
}


// Goes throw linked list and sends all packages
// (my code takes care of max number packets in linked list so i don't need to do it here)
void sendPackages(struct Node* head, struct sockaddr_in dest_addr, int so, int rc){
  struct Node* tmpN = head;
  while(tmpN != NULL){
    printf("Sending: %s...\n", tmpN->name);
    rc = send_packet(so,
                     tmpN->bufData,
                     tmpN->dataSize,
                     0,
                     (struct sockaddr*)&dest_addr,
                     sizeof(struct sockaddr_in));
    error(rc, "send_packet");
    tmpN = tmpN->next;
  }
}


int main(int argc, char* argv[]){
  if(argc < 5){
    printf("usage: ./upclient destIP destPort imageListFile Drop(0-20)\n");
    return EXIT_FAILURE;
  }
  unsigned char upper_bund = 0; // this is also sequenceNum |goes up after we got expected ACK1|
  unsigned short dest_port = atoi(argv[2]); // destination port
  char *file_name, *ip; // given file name and destination ip
  struct sockaddr_in dest_addr, myaddr;
  struct in_addr ip_addr;
  int so, rc, drop;
  socklen_t deslen = sizeof(dest_addr);


  ip = argv[1];
  file_name = argv[3];
  checkForFileExistence(file_name);

  drop = atoi(argv[4]); // from 0 to 20%
  if(drop < 0 || drop > 20){
    printf("Wrong drop rate. Available only from 0 to 20\n");
    return EXIT_FAILURE;
  }
  set_loss_probability((float) drop/100); // from 0 to 1

  // sets localhost as IPv4-address
  inet_pton(AF_INET, ip, &ip_addr);

  // will send with IPv4; with given port and ip_addr
  memset((char *) &dest_addr, 0, sizeof(dest_addr)); // puts 0 to whole struct; Because it will have an arbitrary value upon creation
  dest_addr.sin_family = AF_INET;
  dest_addr.sin_port = htons(dest_port);
  dest_addr.sin_addr = ip_addr;

  // recive messages to this adress with port that client decide
  memset((char *) &myaddr, 0, sizeof(myaddr));
  myaddr.sin_family = AF_INET;
  // Port can be from 1024 to 49151
  // Problem here is that if port of the server is the same as port of client, it won't work
  if(dest_port == 8888){
    myaddr.sin_port = htons(8889);
  }else{
    myaddr.sin_port = htons(8888);
  }
  myaddr.sin_addr.s_addr = htonl(INADDR_ANY);

  // socket that sends datagramms with IPv4
  so = socket(AF_INET, SOCK_DGRAM, 0);
  error(so, "socket");

  // socket that will listen given adress
  rc = bind(so, (struct sockaddr *)&myaddr, sizeof(struct sockaddr_in));
  error(rc, "bind");


  packetSize = malloc(sizeof(int*)); /* global allocated variable that will be changed
                                        for each new created node in linked list */
  packetSize[0] = 0;
  struct Node* head = NULL; // head of linked list
  struct Node* lastAdded = NULL; // last added to linked list (so i don't need to go throw whole linked list)

  // allocate first Node (i guess that txt file have no less than 1 Image file inside, or program will write error)
  head = (struct Node*)malloc(sizeof(struct Node));
  lastAdded = head;
  head->bufData = createpackage(fileNamePointers[(int)upper_bund], upper_bund, 0x01);
  head->next = NULL;
  head->dataSize = packetSize[0];
  head->name = basename(fileNamePointers[(int)upper_bund]);
  upper_bund++; // goes app for each added node in linked list
  // allocate 6 nodes in the heap (so window size will be 7 |max|)
  int six_after_headNode = 6; // to be sure that i don't send more than i have at start
  if(number_of_files[0] < 7){
    six_after_headNode = number_of_files[0]-1;
  }
  lastAdded = addPackagetoList(six_after_headNode, upper_bund, lastAdded);
  upper_bund = upper_bund + six_after_headNode;


  fd_set set;  // variables needed for timer
  struct timeval tv;

  // seting up fd_set
  FD_ZERO(&set);
  FD_SET(so, &set);
  // Timeout of 5 second
  tv.tv_sec = 5;
  tv.tv_usec = 0;

  // sends first 7 packets
  sendPackages(head, dest_addr, so, rc);

  // upper_bund - lower_bund can't be more than 7 (window size); but my linked list will take care of it
  int lower_bund = 0; // goes up after we got expected ACK
  unsigned char* ack = malloc(8); // buffer for answers from server, 8 byte is enough

  while(1){
    // waits for 5 only once (need to restart time manualy)
    // But continue if socket gets message
    rc = select(FD_SETSIZE, &set, NULL, NULL, &tv);
    error(rc, "select");

    // got message from server
    if(rc && FD_ISSET(so, &set)){
      rc = recvfrom(so, ack, 8, 0, (struct sockaddr*) &dest_addr, &deslen);
      error(rc, "recvfrom");

          /* Here i could also check every node in linked list
             for ack[4] that could make whole transfering process
             faster but exam paper ask me to check only the
             "oldest node in linked list" */

      // checks if the first package seq.num in lenkedlist is the same as ACK seq.num
      if(head->bufData[4] == ack[4]){
        printf("Got right ACK|%d|\n", ack[4]);
        lower_bund++;
        // we can delete first element in linked list, add new one at the end, and send it to server right away
        struct Node* temp = head;
        head = head->next; // after last ACK head = NULL;
        free(temp->bufData);
        free(temp);
        if(upper_bund < number_of_files[0]){ // if there is still some files to be send
          lastAdded = addPackagetoList(1, upper_bund, lastAdded);
          upper_bund++;
          // sends it right away
          printf("Sending: %s...\n", lastAdded->name);
          rc = send_packet(so,
                           lastAdded->bufData,
                           lastAdded->dataSize,
                           0,
                           (struct sockaddr*)&dest_addr,
                           sizeof(struct sockaddr_in));
          error(rc, "send_packet");
        }else if(lower_bund == upper_bund){ // no more file to send AND all ACK recieved
          // maing termination packet
          char * termTemp = malloc(8);
          for(int i = 0; i < 8; i++){
            if(i == 6){
              termTemp[i] = 0x04;
            }else{
              termTemp[i] = 0;
            }
          }
          rc = send_packet(so,
                           termTemp,
                           8,
                           0,
                           (struct sockaddr*)&dest_addr,
                           sizeof(struct sockaddr_in));
          error(rc, "send_packet");
          free(termTemp);
          break; // goes out of while loop->free rest of memory->closing client
        }
      }else{// got unexpected ACK = do nothing
        printf("Wrong ACK! |%d| (expected: %d)\n", ack[4], head->bufData[4]);
      }

    }else{ // timeout
      printf("Timeout... Sending all packets of 'window size' again...\n");
      // reset struct and time for select */
      tv.tv_sec = 5;
      tv.tv_usec = 0;
      FD_SET(so, &set);
      // send all the packages in linked list again
      sendPackages(head, dest_addr, so, rc);
    }
  }
  printf("All data have been successfully sent to server!\n");
  free(ack); // free allocated memory for ack's from server
  close(so); // close socket

  // frees malloc for every element
  for(int i = 0; i < number_of_files[0]; i++){
    free(fileNamePointers[i]); // memory was allocated in makeFileNameBuffers function
  }
  free(packetSize);
  free(number_of_files);
  free(fileNamePointers); // memory was allocated in makeFileNameBuffers function

  return EXIT_SUCCESS;
}
