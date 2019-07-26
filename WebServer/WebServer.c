/* http_server.c - http 1.0 server  */

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include "config.h"
#include "helpers.h"
#define PATH_MAX        4096    /* # chars in a path name including nul */

/*------------------------------------------------------------------------
* Program:   http server
*
* Purpose:   allocate a socket and then repeatedly execute the following:
*              (1) wait for the next connection from a client
*              (2) read http request, reply to http request
*              (3) close the connection
*              (4) go back to step (1)
*
* Syntax:    http_server [ port ]
*
*               port  - protocol port number to use
*
* Note:      The port argument is optional.  If no port is specified,
*            the server uses the port specified in config.h
*
*------------------------------------------------------------------------
*/

int main(int argc, char *argv[])
{
  struct  sockaddr_in serv_addr; /* structure to hold server's address  */
  int     listen_socket, connection_socket;
  int     port;
  pid_t   pid;  /* id of child process to handle request */
  char    response_buffer[MAX_HTTP_RESP_SIZE];
  int     status_code;
  char *  status_phrase;

  /* 1) Create a socket */
  listen_socket = socket(AF_INET, SOCK_STREAM, 0);

  /* Check command-line argument for port and extract    */
  /* port number if one is specified.  Otherwise, use default  */

  if (argc > 1) {                 /* if argument specified        */
    port = atoi(argv[1]);   /* convert from string to integer   */
  } else {
    port = DEFAULT_PORT;
  }
  if (port <= 0) {             /* test for legal value       */
    fprintf(stderr, "bad port number %d", port);
    exit(EXIT_FAILURE);
  }


  /* 2) Set the values for the server  address structure:  serv_addr */
  memset(&serv_addr,0,sizeof(serv_addr)); /* clear sockaddr structure */
  serv_addr.sin_addr.s_addr = INADDR_ANY;
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_port = htons(port);

  /* 3) Bind the socket to the address information set in serv_addr */
  if (bind(listen_socket, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
    puts("Bind failed");
  }

  /* 4) Start listening for connections */
  listen(listen_socket, 3);
  puts("Waiting for incoming connection");


  /* Main server loop - accept and handle requests */

  while (true) {

    /* 5) Accept a connection */

    int c = sizeof(struct sockaddr_in);

    struct sockaddr_in client_addr;
    connection_socket = accept(listen_socket, (struct sockaddr *)&client_addr, (socklen_t*)&c);

    if (connection_socket < 0) {
      perror("Accept failed");
    }
    puts("Connection accepted");


    /* Fork a child process to handle this request */

    if ((pid = fork()) == 0) {

      /*----------START OF CHILD CODE----------------*/
      /* we are now in the child process */


      /* child does not need access to listen_socket */
      if ( close(listen_socket) < 0) {
        fprintf(stderr, "child couldn't close listen socket");
        exit(EXIT_FAILURE);
      }

      struct http_request new_request; // defined in httpreq.h
      /* 6) call helper function to read the request         *
      * this will fill in the struct new_request for you *
      * see helper.h and httpreq.h                       */
      puts("___________");
      bool return_state = Parse_HTTP_Request(connection_socket, &new_request);

      /*** PART A:  PRINT OUT
      *   URI, METHOD and return value of  Parse_HTTP_Request()
      */

      printf("%s %s %s\n", new_request.URI, new_request.method, return_state ? "True" : "False");

      /****  PART B ONLY *******/
      /* 7) Decide which status_code and reason phrase to return to client */
      // (new_request.method == "GET") || (new_request.method == "HEAD")
      if ((!strcmp(new_request.method, "GET")) || (!strcmp(new_request.method, "HEAD"))) {

        if (Is_Valid_Resource(new_request.URI)) {
          // if (!strcmp(new_request.method, "GET")) {
            status_code = 200;
            status_phrase = "OK";
          // }
          // else {
          //   status_code = 204;
          //   status_phrase = "No Content";
          // }

        }
        else {
          status_code = 404;
          status_phrase = "Not Found";
        }
      }
      else if (!strcmp(new_request.method, "POST") || !strcmp(new_request.method, "DELETE")
              || !strcmp(new_request.method, "LINK") || !strcmp(new_request.method, "UNLINK")) {
        status_code = 501;
        status_phrase = "Not Implemented";
      }
      else {
        status_code = 400;
        status_phrase = "Bad Request";
      }


      // set the reply to send
      sprintf(response_buffer, "HTTP/1.0 %d %s\r\n", status_code, status_phrase);
      printf("Sending response line: %s\n", response_buffer);
      send(connection_socket, response_buffer, strlen(response_buffer), 0);

      // send resource if requested, under what condition will the server send an
      // entity body?
      if ((status_code == 200) && (strcmp(new_request.method, "HEAD"))) {
        Send_Resource(connection_socket, new_request.URI);
      }
      else if ((status_code == 200) && (!strcmp(new_request.method, "HEAD"))) {
        char * location;
        char * resource;
        char * server_directory;

        if ( (server_directory = (char *) malloc(PATH_MAX)) != NULL)
          getcwd(server_directory, PATH_MAX);

        resource = strstr(new_request.URI, "http://");
        if (resource == NULL) {
          /* no http:// check if first character is /, if not add it */
          if (new_request.URI[0] != '/')
            resource = strcat("/", new_request.URI);
          else
            resource = new_request.URI;
        }
        else
          /* if http:// resource must start with '/' */
          resource = strchr(resource, '/');

        strcat(server_directory, RESOURCE_PATH);
        location = strcat(server_directory, resource);
        /* open file and send contents on socket */

        FILE * file = fopen(location, "r");

        if (file < 0) {
          fprintf(stderr, "Error opening file.\n");
          exit(EXIT_FAILURE);
        }

        fseek(file, 0L, SEEK_END);
        long sz = ftell(file);

        char head_response[MAX_HTTP_RESP_SIZE];
        sprintf(head_response, "Content-Length: %li\r\n\r\n", sz);
        send(connection_socket, head_response, strlen(head_response), 0);
      }
      else {
        // don't need to send resource.  end HTTP headers
        send(connection_socket, "\r\n\r\n", strlen("\r\n\r\n"), 0);
      }

      /***** END PART B  ****/

      /* child's work is done, close remaining descriptors and exit */

      if ( close(connection_socket) < 0) {
        fprintf(stderr, "closing connected socket failed");
        exit(EXIT_FAILURE);
      }

      /* all done return to parent */
      exit(EXIT_SUCCESS);

    }
    /*----------END OF CHILD CODE----------------*/

    /* back in parent process  */
    /* close parent's reference to connection socket, */
    /* then back to top of loop waiting for next request */
    if ( close(connection_socket) < 0) {
      fprintf(stderr, "closing connected socket failed");
      exit(EXIT_FAILURE);
    }

    /* if child exited, wait for resources to be released */
    waitpid(-1, NULL, WNOHANG);

  } // end while(true)
}
