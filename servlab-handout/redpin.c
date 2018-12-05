/*
 * redpin.c - [Starting code for] a web-based manager of people and
 *            places.
 *
 * Based on:
 *  tiny.c - A simple, iterative HTTP/1.0 Web server that uses the 
 *      GET method to serve static and dynamic content.
 *   Tiny Web server
 *   Dave O'Hallaron
 *   Carnegie Mellon University
 */
#include "csapp.h"
#include "dictionary.h"
#include "more_string.h"

static void doit(int fd);
static dictionary_t *read_requesthdrs(rio_t *rp);
static void read_postquery(rio_t *rp, dictionary_t *headers, dictionary_t *d);
static void clienterror(int fd, char *cause, char *errnum, 
                        char *shortmsg, char *longmsg);
static void print_stringdictionary(dictionary_t *d);
static void serve_request(int fd, dictionary_t *query);
int detect_str(dictionary_t *d, char* str);
static void serve_sum(int fd, dictionary_t *query);
void *doit_thread(void* connfd_p);
static void serve_counts(int fd);
static void serve_pin(int fd, dictionary_t *query);

dictionary_t* d_ppl;
dictionary_t* d_plcs;

int ppl_count = 0;
int places_count = 0;
int debug_on = 1;

int main(int argc, char **argv) 
{
  int listenfd, connfd;
  char hostname[MAXLINE], port[MAXLINE];
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;

  d_ppl = make_dictionary(COMPARE_CASE_SENS, free);
  char* name1 = strdup("alice");
  char* name2 = strdup("bob");
  
  dictionary_t* tmp1 = make_dictionary(COMPARE_CASE_SENS, free);

  /* v FOR TESTING v */

  char* tmp1_plcs = strdup("London");
  char* tmp1_plcs2 = strdup("Paris");
  dictionary_set(tmp1, tmp1_plcs, NULL);  
  dictionary_set(tmp1, tmp1_plcs2, NULL);    
  free(tmp1_plcs);
  free(tmp1_plcs2);  

  dictionary_set(d_ppl, name1, tmp1);
  dictionary_set(d_ppl, name2, NULL);
  free(name1);
  free(name2);

  printf("PPL COUNT: %zu\n", dictionary_count(d_ppl));

  dictionary_t* alice_d = (dictionary_t*)dictionary_get(d_ppl , "alice");
  printf("ALICE PLCS COUNT: %zu\n", dictionary_count(alice_d));

  printf("init print\n");
  print_stringdictionary(d_ppl);
  printf("Alice places\n");
  print_stringdictionary((dictionary_t*)dictionary_get(d_ppl, "alice"));

  /* ^ FOR TESTING  ^ */


  /* Check command line args */
  if (argc != 2) {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }


/*  
 * open_listenfd - open and return a listening socket on port
 *     Returns -1 and sets errno on Unix error.
 */
  listenfd = Open_listenfd(argv[1]);

  /* Don't kill the server if there's an error, because
     we want to survive errors due to a client. But we
     do want to report errors. */
  exit_on_error(0);

  /* Also, don't stop on broken connections: */
  /*In the case of a broken pipe which wasn't closed on one end, no error is thrown*/
  Signal(SIGPIPE, SIG_IGN);

/*
dictionary_t *make_dictionary(int compare_mode, free_proc_t free_value)
{
  dictionary_t *d = malloc(sizeof(dictionary_t));

  d->fill_count = 0;
  d->map_count = 0;
  d->array_size = 16;
  d->keys = calloc(d->array_size, sizeof(char *));
  d->vals = calloc(d->array_size, sizeof(void *));
  d->is_ci = (compare_mode == COMPARE_CASE_INSENS);
  d->free_value = free_value;

  return d;
}

*/




  while (1) {
    clientlen = sizeof(clientaddr);
    connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
    if (connfd >= 0) {
      Getnameinfo((SA *) &clientaddr, clientlen, hostname, MAXLINE, 
                  port, MAXLINE, 0);
      printf("Accepted connection from (%s, %s)\n", hostname, port);
      /*doit_thread(connfd);*/ // old

      pthread_t t1;
      int* connfd_p = malloc(sizeof(int));
      *connfd_p = connfd;
      Pthread_create(&t1, NULL, doit_thread, connfd_p);

      /*Close(connfd); */ //old
    }
  }
}

void *doit_thread(void* connfd_p){
  if(debug_on)printf("Enter new thread\n");  
  int connfd = *(int*)connfd_p;
  free(connfd_p);
  doit(connfd);
  Close(connfd);
  if(debug_on)printf("Thread Exiting\n");
  return NULL;
}

/*
 * doit - handle one HTTP request/response transaction
 */
void doit(int fd) 
{
  char buf[MAXLINE], *method, *uri, *version;
  rio_t rio;
  dictionary_t *headers, *query;

  /* Read request line and headers */

  /*
  The rio readinitb function is called once per open descriptor. It associates the descriptor fd with a
  read buffer of type rio t at address rp.
  */
  Rio_readinitb(&rio, fd);
  if (Rio_readlineb(&rio, buf, MAXLINE) <= 0){
    printf("READLINE ERROR !!!\n");
    return;
  }
  printf("%s", buf);
  
  if (!parse_request_line(buf, &method, &uri, &version)) {
    clienterror(fd, method, "400", "Bad Request",
                "Redpin did not recognize the request");
  } else {
    if (strcasecmp(version, "HTTP/1.0")
        && strcasecmp(version, "HTTP/1.1")) {
      clienterror(fd, version, "501", "Not Implemented",
                  "Redpin does not implement that version");
    } else if (strcasecmp(method, "GET")
               && strcasecmp(method, "POST")) {
      clienterror(fd, method, "501", "Not Implemented",
                  "Redpin does not implement that method");
    } else {
      headers = read_requesthdrs(&rio);

      printf("______________________________________________\nHEADERS:\n-----\n");
      print_stringdictionary(headers);
      printf("\n\n\n_____________________________________________\n");

      /* Parse all query arguments into a dictionary */
      query = make_dictionary(COMPARE_CASE_SENS, free);
      parse_uriquery(uri, query);
      if (!strcasecmp(method, "POST"))
        read_postquery(&rio, headers, query);


      //detect_str(query, "alice");
      /* For debugging, print the dictionary */
      printf("______________________________________________\nQUERY:\n-----\n");
      print_stringdictionary(query);
      printf("\n\n\n_____________________________________________\n");
      printf("COUNT: %zu\n", dictionary_count(query));


      if(starts_with("/counts", uri))           {if(debug_on)printf("counts\n"); serve_counts(fd);}
      else if(starts_with("/reset", uri))       {if(debug_on)printf("reset\n"); /*serve_reset(fd, query);*/}
      else if(starts_with("/people", uri))      {if(debug_on)printf("people\n"); /*serve_people(fd, query);*/}      
      else if(starts_with("/places", uri))      {if(debug_on)printf("places\n"); /*serve_places(fd, query);*/}
      else if(starts_with("/pin", uri))         {if(debug_on)printf("pin\n"); serve_pin(fd, query);}
      else if(starts_with("/unpin", uri))       {if(debug_on)printf("unpin\n"); /*serve_unpin(fd, query);*/}      
      else if(starts_with("/copy", uri))        {if(debug_on)printf("copy\n"); /*serve_copy(fd, query);*/}      
      else if(starts_with("/sum", uri))         {if(debug_on)printf("sum\n"); serve_sum(fd, query);}      
      else                                      {if(debug_on)printf("8\n"); /*serve_request(fd, query);*/}

      /* You'll want to handle different queries here,
         but the intial implementation always returns
         nothing: 
      */

      /*serve_request(fd, query); */

      /* Clean up */
      dictionary_free(query);
      dictionary_free(headers);
    }

    /* Clean up status line */
    free(method);
    free(uri);
    free(version);
  }
}

/*
 * read_requesthdrs - read HTTP request headers
 */
dictionary_t *read_requesthdrs(rio_t *rp) 
{
  char buf[MAXLINE];
  dictionary_t *d = make_dictionary(COMPARE_CASE_INSENS, free);

  Rio_readlineb(rp, buf, MAXLINE);
  while(strcmp(buf, "\r\n")) {
    printf("%s", buf);
    parse_header_line(buf, d);
    Rio_readlineb(rp, buf, MAXLINE);
  }
  
  return d;
}

void read_postquery(rio_t *rp, dictionary_t *headers, dictionary_t *dest)
{
  char *len_str, *type, *buffer;
  int len;
  
  len_str = dictionary_get(headers, "Content-Length");
  len = (len_str ? atoi(len_str) : 0);

  type = dictionary_get(headers, "Content-Type");
  
  buffer = malloc(len+1);
  Rio_readnb(rp, buffer, len);
  buffer[len] = 0;

  if (!strcasecmp(type, "application/x-www-form-urlencoded")) {
    parse_query(buffer, dest);
  }

  free(buffer);
}

static char *ok_header(size_t len, const char *content_type) {
  char *len_str, *header;
  
  header = append_strings("HTTP/1.0 200 OK\r\n",
                          "Server: Redpin Web Server\r\n",
                          "Connection: close\r\n",
                          "Content-length: ", len_str = to_string(len), "\r\n",
                          "Content-type: ", content_type, "\r\n\r\n",
                          NULL);
  free(len_str);

  return header;
}

/*
 * serve_request - example request handler
 */
static void serve_request(int fd, dictionary_t *query)
{
  size_t len;
  char *body, *header;



  body = strdup("alice\nbob\n");

  len = strlen(body);

  /* Send response headers to client */
  header = ok_header(len, "text/plain; charset=utf-8");
  Rio_writen(fd, header, strlen(header));
  printf("Response headers:\n");
  printf("%s", header);

  free(header);

  /* Send response body to client */
  Rio_writen(fd, body, len);

  free(body);
}

static void serve_pin(int fd, dictionary_t *query)
{
  size_t len;
  char *body, *header;


  if(d_ppl == NULL){
    printf("EMPTY DICTIONARY SERV_PIN\n");
    d_ppl = make_dictionary(COMPARE_CASE_SENS, free);
  }

  body = strdup("alice\nbob\n");

  len = strlen(body);

  /* Send response headers to client */
  header = ok_header(len, "text/plain; charset=utf-8");
  Rio_writen(fd, header, strlen(header));
  printf("Response headers:\n");
  printf("%s", header);

  free(header);

  /* Send response body to client */
  Rio_writen(fd, body, len);

  free(body);
}

static void serve_counts(int fd)
{
  size_t len;
  char *body, *header;



  if(d_ppl == NULL){
    body = strdup(append_strings("0\n0", "\n",NULL));
    printf("NOBODY\n");
  }


  char* ppl_cnt = strdup(to_string(dictionary_count(d_ppl)));
  printf("PPL COUNT in counts: %zu\n", dictionary_count(d_ppl));

  int places_sum = 0;

  int i;
  printf("STARTING PLACES\n");
  char** keys = (char**)dictionary_keys(d_ppl);
  for(i = 0; keys[i] != NULL; i++){
    printf("keys %s\n", keys[i]);
    if(dictionary_get(d_ppl, keys[i]) != NULL)
    places_sum += dictionary_count(dictionary_get(d_ppl, keys[i]));
  }

  printf("Plcs coount: %i\n", places_sum);
  char* plcs_cnt = strdup(to_string(places_sum));

  printf("PRINTING APPENDED BODY\n");
  body = append_strings(ppl_cnt,"\n", plcs_cnt, "\n", NULL);
  len = strlen(body);

  /* Send response headers to client */
  header = ok_header(len, "text/plain; charset=utf-8");
  Rio_writen(fd, header, strlen(header));
  printf("Response headers:\n");
  printf("%s", header);

  free(header);

  /* Send response body to client */
  Rio_writen(fd, body, len);
  free(ppl_cnt);
  free(plcs_cnt);
  free(body);

}


static void serve_sum(int fd, dictionary_t *query)
{ 
  size_t len;
  char *body, *header;
  void* arg1, *arg2;
  arg1 = dictionary_get(query,"x");
  arg2 = dictionary_get(query,"y");

  int x, y, result;
  char* temp = NULL;
  if(arg1 == NULL) body = strdup("Please provide numbers: x was not specified\n");
  else if(arg2 == NULL)body = strdup("Please provide numbers: y was not specified\n");
  else{
    x = atoi(arg1);
    y = atoi(arg2);
    result = x + y;
    temp = strdup(to_string(result));
    body = strdup(append_strings(temp, "\n",NULL));
    sleep(30);
  }

  len = strlen(body);

  /* Send response headers to client */
  header = ok_header(len, "text/html; charset=utf-8");
  Rio_writen(fd, header, strlen(header));
  printf("Response headers:\n");
  printf("%s", header);

  free(header);

  /* Send response body to client */
  Rio_writen(fd, body, len);
  free(temp);
  free(body);
}
/*
 * clienterror - returns an error message to the client
 */
void clienterror(int fd, char *cause, char *errnum, 
		 char *shortmsg, char *longmsg) 
{
  size_t len;
  char *header, *body, *len_str;

  body = append_strings("<html><title>Redpin Error</title>",
                        "<body bgcolor=""ffffff"">\r\n",
                        errnum, " ", shortmsg,
                        "<p>", longmsg, ": ", cause,
                        "<hr><em>Redpin Server</em>\r\n",
                        NULL);
  len = strlen(body);

  /* Print the HTTP response */
  header = append_strings("HTTP/1.0 ", errnum, " ", shortmsg, "\r\n",
                          "Content-type: text/html; charset=utf-8\r\n",
                          "Content-length: ", len_str = to_string(len), "\r\n\r\n",
                          NULL);
  free(len_str);
  
  Rio_writen(fd, header, strlen(header));
  Rio_writen(fd, body, len);

  free(header);
  free(body);
}

static void print_stringdictionary(dictionary_t *d)
{
  int i;
  const char **keys;

  keys = dictionary_keys(d);
  
  for (i = 0; keys[i] != NULL; i++) {
    printf("%s=%s\n",
           keys[i],
           (const char *)dictionary_get(d, keys[i]));
  }
  printf("\n");

  free(keys);
}

/*
int detect_str(dictionary_t *d, char* str)
{
  int i;
  const char **keys;

  keys = dictionary_keys(d);
  
  for (i = 0; keys[i] != NULL; i++) {
  
    //printf("%s=%s\n",
    //       keys[i],
    //       (const char *)dictionary_get(d, keys[i]));
  
    if(strcmp(str, keys[i]) == 0)
      printf("MATCH DETECTED\n");
    else
      printf("NO MATCH\n");
  }
  printf("\n");

  free(keys);
}

*/
