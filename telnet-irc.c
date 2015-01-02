/**
 * @file  telnet-irc.c
 * @brief telnet-irc
 *
 * An IRC telnet client (specialized to prevent ping timeouts)
 *
 * @author     Clay Freeman
 * @date       December 31, 2014
 */

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <event2/event.h>
#include <fcntl.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

char* getIPFromHost(const char* name);
void printUsage(char* binary);
int processPing(const char* data);
static void socketEventCallback(int fd, short events, void* ptr);
void startEvents();
static void stdinEventCallback(int fd, short events, void* ptr);

int sock;
struct timeval timeout;

int main(int argc, char** argv) {
  // Prepare the timeout global variable
  timeout.tv_sec = 0;
  timeout.tv_usec = 1;

  // Assume error status until all checks are cleared
  int status = -1;
  // Make sure we have a hostname and port
  if (argc > 1) {
    // Set status to normal; checks were passed
    // status = 0;

    // Setup storage necessary to make a connection
    int port = 6667;

    // Check if a non-default port was provided
    if (argc > 2) {
      port = atoi(argv[2]);
      if (DEBUG == 1) printf("DEBUG: Parsed non-default port as %i\n", port);
    }

    if (port != 0) {
      // Print provided parameters
      if (DEBUG == 1) printf("DEBUG: %s %i\n", argv[1], port);

      // Get the IP address for the given hostname
      char* addr_ptr = getIPFromHost(argv[1]);
      if (addr_ptr != NULL) {
        // Copy the address safely from static storage
        char* addr = calloc(strlen(addr_ptr) + 1, sizeof(char));
        memcpy(addr, addr_ptr, strlen(addr_ptr));
        addr_ptr = 0;

        // Print the resolved address
        if (DEBUG == 1) printf("DEBUG: %s\n", addr);

        // Attempt to connect to the host
        sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock >= 0) {
          struct sockaddr_in serv_addr;
          // Zero the memory for serv_addr
          memset((char*)&serv_addr, 0, sizeof(serv_addr));
          // Setup required properties
          serv_addr.sin_family = AF_INET;
          serv_addr.sin_addr.s_addr = inet_addr(addr);
          serv_addr.sin_port = htons(port);
          printf("Trying %s...\n", addr);
          int s = connect(sock, (struct sockaddr*)&serv_addr,
            sizeof(serv_addr));
          if (s >= 0) {
            // We connected!
            status = 0;
            startEvents();
          }
          else {
            printf("Error: Could not connect to host\n");
          }
        }
        else {
          printf("Error: Could not create socket\n");
        }

        // Clean up our address
        free(addr);
        addr = 0;
      }
      else {
        printf("Error: Could not resolve provided host\n");
      }
    }
    else {
      printf("Error: The provided port was invalid\n\n");
      printUsage(argv[0]);
    }
  }
  else {
    printf("Error: No host provided\n\n");
    printUsage(argv[0]);
  }
  // Return the exit status
  return status;
}

/**
 * @brief Get IP from Host
 *
 * Gets the first IP address (A record) entry for a given hostname
 *
 * @remarks
 * The pointer that is returned is static storage and should be copied
 *   immediately; it is required that you null the pointer without freeing
 *
 * @param name The hostname in which to query for an IP address
 *
 * @return A C-String to the IP address
 */
char* getIPFromHost(const char* name) {
  char* retVal = NULL;
  struct hostent* host = gethostbyname(name);
  if (host != NULL) {
    retVal = inet_ntoa(*(struct in_addr*)host->h_addr_list[0]);
  }
  return retVal;
}

/**
 * @brief Print Usage
 *
 * Prints the usage guide for this program
 *
 * @param binary Preferably argv[0], but "telnet-irc" would suffice
 */
void printUsage(char* binary) {
  printf("Usage: %s <host> [port]\n", binary);
  printf("Examples:\n");
  printf("  %s irc.freenode.net\n", binary);
  printf("  %s irc.example.org 6669\n", binary);
}

/**
 * @brief Process Ping
 *
 * Detect if a PING request was sent by the peer
 *
 * @remarks
 *   This function expects the request command to be capitalized
 *
 * @param data The string of data to check for a PING request
 *
 * @return 0 if no request was found, 1 if a request was found
 */
int processPing(const char* data) {
  int retVal = 0;
  // Test if "PING" is present
  if (strstr(data, "PING")) {
    // Update return value
    retVal = 1;

    // Allocate memory
    char* buffer = NULL;
    char* source = calloc(strlen(data), sizeof(char));
    // Grab PING request's source
    sscanf(data, "PING %s\n", source);
    // Allocate buffer space (PONG, space, \n, and null-terminator)
    buffer = calloc(strlen(source) + 7, sizeof(char));
    // Format buffer with reply
    sprintf(buffer, "PONG %s\n", source);
    // Send reply
    write(sock, buffer, strlen(buffer));
    if (DEBUG == 1) printf("DEBUG: %sDEBUG: %s", data, buffer);
    // Clean up memory
    free(buffer);
    free(source);
    buffer = 0;
    source = 0;
  }
  return retVal;
}

/**
 * @brief Socket Event Callback
 *
 * A callback for when there is data to read on the connection created at setup
 *
 * @param fd     File descriptor for the socket
 * @param events A series of flags that are useless to this function
 * @param ptr    Pointer to an optional parameter (not used)
 */
static void socketEventCallback(int fd, short events, void* ptr) {
  int count;
  ioctl(fd, FIONREAD, &count);
  while (count > 0) {
    char* data = calloc(1025, sizeof(char));
    read(fd, data, 1024);
    if (!processPing(data)) {
      printf("%s", data);
    }
    else {
      if (DEBUG == 1) printf("DEBUG: Automatically responded to PING\n");
    }
    free(data);
    data = 0;
    ioctl(fd, FIONREAD, &count);
  }

  events = 0;
  ptr = 0;
}

/**
 * @brief Start Events
 *
 * Responsible for starting the events associated with reading stdin and sock
 *
 * @remarks
 * Probably won't ever reach the "Free memory" section, but it can remain
 *   there for posterity
 */
void startEvents() {
  // Prepare the base
  struct event_base* base = event_base_new();

  // Prepare socket event
  struct event* socketEvent = event_new(base, sock, EV_TIMEOUT | EV_PERSIST |
    EV_READ, socketEventCallback, base);
  event_base_set(base, socketEvent);
  event_add(socketEvent, &timeout);

  // Prepare stdin event
  struct event* stdinEvent = event_new(base, fileno(stdin), EV_TIMEOUT |
    EV_PERSIST | EV_READ, stdinEventCallback, base);
  event_base_set(base, stdinEvent);
  event_add(stdinEvent, &timeout);

  // Start events
  event_base_loop(base, 0x04);

  // Free memory
  event_free(socketEvent);
  event_free(stdinEvent);
  event_base_free(base);
  socketEvent = 0;
  stdinEvent = 0;
  base = 0;
}

/**
 * @brief stdin Event Callback
 *
 * A callback for when there is data to read on stdin
 *
 * @param fd     File descriptor for the socket
 * @param events A series of flags that are useless to this function
 * @param ptr    Pointer to an optional parameter (not used)
 */
static void stdinEventCallback(int fd, short events, void* ptr) {
  int count;
  ioctl(fd, FIONREAD, &count);
  while (count > 0) {
    char* data = calloc(1025, sizeof(char));
    read(fd, data, 1024);
    write(sock, data, strlen(data));
    free(data);
    data = 0;
    ioctl(fd, FIONREAD, &count);
  }

  events = 0;
  ptr = 0;
}
