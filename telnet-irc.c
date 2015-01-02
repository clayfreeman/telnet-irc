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
#include <event2/event.h>
#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

void clean();
char* getIPFromHost(const char* name);
void handleSignals(int sig);
void printUsage(const char* binary);
int processPing(const char* data);
static void pipeEventCallback(int fd, short events, void* ptr);
void startEvents();
static void stdinEventCallback(int fd, short events, void* ptr);

// Setup required global storage
struct event_base* base = NULL;
char* addr = NULL;
pid_t pid = 0;
int rpipe[2], wpipe[2];
struct event* pipeEvent = NULL;
struct event* stdinEvent = NULL;
struct timeval timeout;

int main(int argc, char** argv) {
  // Prepare the timeout global variable
  timeout.tv_sec = 0;
  timeout.tv_usec = 1;

  // Setup required storage
  char* addr_ptr = NULL;
  int port = 6667;
  int status = -1;

  // Make sure we have at least a hostname
  if (argc > 1) {
    // Check if a non-default port was provided
    if (argc > 2) {
      port = atoi(argv[2]);
      if (DEBUG == 1) printf("DEBUG: Parsed non-default port as %i\n", port);
    }

    if (port > 0 && port < 65536) {
      // Print provided parameters
      if (DEBUG == 1) printf("DEBUG: Got args: %s %i\n", argv[1], port);

      // Get the IP address for the given hostname
      addr_ptr = getIPFromHost(argv[1]);
      if (addr_ptr != NULL) {
        // Copy the address safely from static storage
        addr = calloc(strlen(addr_ptr) + 1, sizeof(char));
        memcpy(addr, addr_ptr, strlen(addr_ptr));
        addr_ptr = NULL;

        // Print the resolved address
        if (DEBUG == 1) printf("DEBUG: IP: %s\n", addr);

        // Prepare port as string
        char pstr[6];
        sprintf(pstr, "%i", port);

        // Setup pipe
        pipe(rpipe);
        pipe(wpipe);
        // Setup signal handlers
        if (signal(SIGCHLD, handleSignals) == SIG_ERR) {
          clean();
          exit(1);
        }
        if (signal(SIGINT, handleSignals) == SIG_ERR) {
          clean();
          exit(1);
        }
        // Fork and exec
        pid = fork();
        if (pid == 0) {
          // Prepare pipes
          close(rpipe[0]);
          close(wpipe[1]);
          dup2(rpipe[1], STDOUT_FILENO);
          dup2(wpipe[0], STDIN_FILENO);
          close(rpipe[1]);
          close(wpipe[0]);

          // Run command
          execl(TELNET, basename(TELNET), addr, pstr, NULL);

          // Exit if something goes wrong
          exit(1);
        }
        else {
          // Prepare pipes
          close(rpipe[1]);
          close(wpipe[0]);

          startEvents();
        }
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

  // Clean up our pointers
  clean();

  // Return the exit status
  return status;
}

/**
 * @brief Clean
 *
 * Frees global pointers and nulls them
 *
 * @remarks
 *   Variables are hardcoded; this function must be updated for new globals
 */
void clean() {
  if (base != NULL) event_base_loopbreak(base); // Prevent event errors
  // Free memory
  if (addr != NULL) free(addr);
  if (pipeEvent != NULL) { event_del(pipeEvent); event_free(pipeEvent); }
  if (stdinEvent != NULL) { event_del(stdinEvent); event_free(stdinEvent); }
  if (base != NULL) event_base_free(base);
  // Null pointers
  addr = NULL;
  pipeEvent = NULL;
  stdinEvent = NULL;
  base = NULL;
  if (DEBUG == 1) printf("DEBUG: All clean!\n");
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
 * @brief Handle Signals
 *
 * Handles any registered signal, cleans up memory, and exits
 *
 * @param sig The signal that was received
 */
void handleSignals(int sig) {
  if (sig == SIGINT) {
    // Try to hide ^C
    printf("\b\b\r");
  }
  if (DEBUG == 1) printf("DEBUG: Caught signal: %i\n", sig);
  // Clean storage
  clean();
  // Close pipes
  close(rpipe[0]);
  close(rpipe[1]);
  close(wpipe[0]);
  close(wpipe[1]);
  // Wait for child
  waitpid((pid_t)(-1), 0, WNOHANG);
  // Exit
  exit(0);
}

/**
 * @brief Print Usage
 *
 * Prints the usage guide for this program
 *
 * @param binary Preferably argv[0], but "telnet-irc" would suffice
 */
void printUsage(const char* binary) {
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
    write(wpipe[1], buffer, strlen(buffer));
    if (DEBUG == 1) printf("DEBUG: %sDEBUG: %s", data, buffer);
    // Clean up memory
    free(buffer);
    free(source);
    buffer = NULL;
    source = NULL;
  }
  return retVal;
}

/**
 * @brief Pipe Event Callback
 *
 * A callback for when there is data to read on the connection created at setup
 *
 * @param fd     File descriptor for the stream
 * @param events A series of flags that are useless to this function
 * @param ptr    Pointer to an optional parameter (not used)
 */
static void pipeEventCallback(int fd, short events, void* ptr) {
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
    data = NULL;
    ioctl(fd, FIONREAD, &count);
  }

  // To prevent unused parameter warnings
  events = 0 / events;
  ptr = NULL;
  ptr = (void*)((char*)ptr + 0);
}

/**
 * @brief Start Events
 *
 * Responsible for starting the events associated with reading stdin and pipefd
 */
void startEvents() {
  // Prepare the base
  base = event_base_new();

  // Prepare pipe event
  pipeEvent = event_new(base, rpipe[0], EV_PERSIST | EV_READ,
    pipeEventCallback, base);
  event_base_set(base, pipeEvent);
  event_add(pipeEvent, &timeout);

  // Prepare stdin event
  stdinEvent = event_new(base, STDOUT_FILENO, EV_PERSIST | EV_READ,
    stdinEventCallback, base);
  event_base_set(base, stdinEvent);
  event_add(stdinEvent, &timeout);

  // Start events
  event_base_loop(base, 0x04);
}

/**
 * @brief stdin Event Callback
 *
 * A callback for when there is data to read on stdin
 *
 * @param fd     File descriptor for the stream
 * @param events A series of flags that are useless to this function
 * @param ptr    Pointer to an optional parameter (not used)
 */
static void stdinEventCallback(int fd, short events, void* ptr) {
  int count;
  ioctl(fd, FIONREAD, &count);
  while (count > 0) {
    char* data = calloc(1025, sizeof(char));
    read(fd, data, 1024);
    // printf("%s", data);
    write(wpipe[1], data, strlen(data));
    free(data);
    data = NULL;
    ioctl(fd, FIONREAD, &count);
  }

  // To prevent unused parameter warnings
  events = 0 / events;
  ptr = NULL;
  ptr = (void*)((char*)ptr + 0);
}
