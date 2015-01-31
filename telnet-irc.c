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
#include "procmanage/procmanage.h"

char* getIPFromHost(const char* name);
void handleSignals(int sig);
void printUsage(const char* binary);
int processPing(const char* data);
static void pipeEventCallback(int fd, short events, void* ptr);
void startEvents();
static void stdinEventCallback(int fd, short events, void* ptr);

// Setup required global storage
struct event_base* base = NULL;
struct Process* p = NULL;

int main(int argc, char** argv, char** envp) {
  // Disable buffering
  setbuf(stdout, NULL);
  // Setup required storage
  char* addr = NULL;
  char* addr_ptr = NULL;
  int port = 6667;
  char pstr[6];
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
        sprintf(pstr, "%i", port);

        // Setup signal handlers; if the child dies, or we get ^C, clean & exit
        // by breaking the event loop
        if (signal(SIGCHLD, handleSignals) != SIG_ERR &&
            signal(SIGINT, handleSignals) != SIG_ERR) {
          // Create a telnet Process
          p = process_create(TELNET, NULL, NULL);
          // Assign the arguments for this Process (first being path, as per
          // Linux standard, followed by address and port)
          process_add_arg(p, TELNET);
          process_add_arg(p, addr);
          process_add_arg(p, pstr);
          // Copy the environment of the local process to the telnet Process
          // (This is probably unnecessary, but might be useful to someone)
          process_add_envs(p, envp);

          // Clean up the address (we no longer need it after creating Process;
          // it's also better to free now to leak less memory upon unexpected
          // crashes and the like)
          free(addr);
          addr = NULL;

          // Open the Process (start it) and prepare the events and start the
          // event loop
          process_open(p);
          startEvents();

          // Clean up the Process (after it exits, or we get ^C; essentially the
          // event loop terminated)
          process_close(p);
          process_free(p);
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

  if (DEBUG == 1) printf("DEBUG: Exiting from main()\n");

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
 * @brief Handle Signals
 *
 * Handles any registered signal
 *
 * @param sig The signal that was received
 */
void handleSignals(int sig) {
  if (sig == SIGINT) {
    // (Try to) hide ^C
    printf("\b\b\r");
  }
  if (DEBUG == 1) printf("DEBUG: Caught signal: %i\n", sig);
  // Close the event loop
  if (base != NULL) event_base_loopbreak(base);
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
  char* buffer = NULL;
  char* source = NULL;
  int retVal = 0;
  // Test if "PING" is present
  if (strstr(data, "PING")) {
    // Update return value
    retVal = 1;

    // Allocate memory for the server source (should be less than strlen(data),
    // but strlen(data) is more than sufficient)
    source = calloc(strlen(data), sizeof(char));
    // Grab PING request's server source
    sscanf(data, "PING %s\n", source);
    // Allocate buffer space for response (PONG, space, source, \n, and
    // null-terminator)
    buffer = calloc(strlen(source) + 7, sizeof(char));
    // Format buffer with standard reply (with source)
    sprintf(buffer, "PONG %s\n", source);
    // Send reply by writing to Process's stdin
    write(p->in, buffer, strlen(buffer));
    if (DEBUG == 1) printf("DEBUG: %sDEBUG: %s", data, buffer);
    // Clean up memory (used by source and buffer)
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
  char* data = NULL;
  // Determine the length of data to read
  ioctl(fd, FIONREAD, &count);
  // While there is data to read...
  while (count > 0) {
    // Allocate space and read the appropriate size of data
    data = calloc(1025, sizeof(char));
    read(fd, data, 1024);

    // Attempt to process a PING command (if there is one), otherwise echo data
    if (!processPing(data)) {
      printf("%s", data);
    }
    else {
      if (DEBUG == 1) printf("DEBUG: Automatically responded to PING\n");
    }
    // Clean up memory associated with the data buffer
    free(data);
    data = NULL;
    // Re-check pending data length
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
  // Prepare the timeout variable
  struct timeval timeout;
  timeout.tv_sec = 0;
  timeout.tv_usec = 10000;

  // Prepare the base
  base = event_base_new();

  // Prepare pipe event
  struct event* pipeEvent = event_new(base, p->out, EV_PERSIST | EV_READ,
    pipeEventCallback, base);
  event_base_set(base, pipeEvent);
  event_add(pipeEvent, &timeout);

  // Prepare stdin event
  struct event* stdinEvent = event_new(base, STDIN_FILENO, EV_PERSIST |
    EV_READ, stdinEventCallback, base);
  event_base_set(base, stdinEvent);
  event_add(stdinEvent, &timeout);

  // Start events
  event_base_loop(base, 0x04);

  // Clean up events
  event_del(pipeEvent);
  event_free(pipeEvent);
  event_del(stdinEvent);
  event_free(stdinEvent);
  event_base_free(base);
  pipeEvent = NULL;
  stdinEvent = NULL;
  base = NULL;
}

/**
 * @brief stdin Event Callback
 *
 * A callback for when there is data to read on stdin (from our perspective)
 *
 * @param fd     File descriptor for the stream
 * @param events A series of flags that are useless to this function
 * @param ptr    Pointer to an optional parameter (not used)
 */
static void stdinEventCallback(int fd, short events, void* ptr) {
  int count;
  char* data = NULL;
  // Determine the length of data to read
  ioctl(fd, FIONREAD, &count);
  // While there is data to read...
  while (count > 0) {
    // Allocate space and read the appropriate size of data
    data = calloc(1025, sizeof(char));
    read(fd, data, 1024);

    // Write the data read from our stdin to stdin of the Process
    // printf("%s", data);
    write(p->in, data, strlen(data));
    // Clean up memory associated with the data buffer
    free(data);
    data = NULL;
    // Re-check pending data length
    ioctl(fd, FIONREAD, &count);
  }

  // To prevent unused parameter warnings
  events = 0 / events;
  ptr = NULL;
  ptr = (void*)((char*)ptr + 0);
}
