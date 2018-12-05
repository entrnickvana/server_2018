/*
 * friendlist.c - [Starting code for] a web-based friend-graph manager.
 *
 * Based on:
 *  tiny.c - A simple, iterative HTTP/1.0 Web server that uses the 
 *      GET method to serve static and dynamic content.
 *   Tiny Web server
 *   Dave O'Hallaron
 *   Carnegie Mellon University
 */

/*  TO DO
  1. Make befriend reciprocal
  2. Finish unfriend for multiple users
  3. Introduce
  4. 
  8. Concurrency

*/

#include "csapp.h"
#include "dictionary.h"
#include "more_string.h"

struct dictionary_t {
  int compare_mode;
  free_proc_t free_value;
  size_t count, alloc;
  const char **keys;
  void **values;
};

dictionary_t* d;

int debug_on = 1;


void doit(int fd); 
void *doit_thread(void* connfd);
static dictionary_t *read_requesthdrs(rio_t *rp);
static void read_postquery(rio_t *rp, dictionary_t *headers, dictionary_t *d);
static void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);
static void print_stringdictionary(dictionary_t *d);
static void serve_request(int fd, dictionary_t *query);
static void serve_introduce(int fd, dictionary_t *query);
static void serve_befriend(int fd, dictionary_t *query);
static void serve_friends(int fd, dictionary_t *query);
static void serve_unfriend(int fd, dictionary_t *query);
static void serve_sum(int fd, dictionary_t *query);
static int contains(dictionary_t* d, char* given_key);

int main(int argc, char **argv) 
{
  int listenfd, connfd;
  char hostname[MAXLINE], port[MAXLINE];
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;

  if(d == NULL)
  d = make_dictionary(0, free);


  /* Check command line args */
  if (argc != 2) {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }

  listenfd = Open_listenfd(argv[1]);

  /* Don't kill the server if there's an error, because
     we want to survive errors due to a client. But we
     do want to report errors. */
  exit_on_error(0);
  
  /* Also, don't stop on broken connections: */
  Signal(SIGPIPE, SIG_IGN);
  
  while (1) {
    clientlen = sizeof(clientaddr);
    connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
    if (connfd >= 0) {
      Getnameinfo((SA *) &clientaddr, clientlen, hostname, MAXLINE, 
                  port, MAXLINE, 0);
      printf("Accepted connection from (%s, %s)\n", hostname, port);
      pthread_t t1;
      int* connfd_p = malloc(sizeof(int));
      *connfd_p = connfd;
      Pthread_create(&t1, NULL, doit_thread, connfd_p);
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
  dictionary_t *headers = NULL, *query = NULL;
  
  /* Read request line and headers */
  Rio_readinitb(&rio, fd);
  if (Rio_readlineb(&rio, buf, MAXLINE) <= 0)
    return;
  printf("%s", buf);
  
  if (!parse_request_line(buf, &method, &uri, &version)) {
    clienterror(fd, method, "400", "Bad Request",
                "Friendlist did not recognize the request");
  } else {
    if (strcasecmp(version, "HTTP/1.0")
        && strcasecmp(version, "HTTP/1.1")) {
      clienterror(fd, version, "501", "Not Implemented",
                  "Friendlist does not implement that version");
    } else if (strcasecmp(method, "GET")
               && strcasecmp(method, "POST")) {
      clienterror(fd, method, "501", "Not Implemented",
                  "Friendlist does not implement that method");
    } else {
      headers = read_requesthdrs(&rio);

      /* Parse all query arguments into a dictionary */
      query = make_dictionary(COMPARE_CASE_SENS, free);
      parse_uriquery(uri, query);

      if (!strcasecmp(method, "POST"))
        read_postquery(&rio, headers, query);
   
      /* For debugging, print the dictionary */
      print_stringdictionary(query);
   
      /* You'll want to handle different queries here,
         but the intial implementation always returns
         nothing: */
   
      if(starts_with("/friends", uri))          {if(debug_on)printf("0\n"); serve_friends(fd, query);}
      else if(starts_with("/befriend", uri))    {if(debug_on)printf("1\n"); serve_befriend(fd, query);}
      else if(starts_with("/unfriend", uri))    {if(debug_on)printf("2\n"); serve_unfriend(fd, query);}
      else if(starts_with("/introduce", uri))   {if(debug_on)printf("3\n"); serve_introduce(fd, query);}
      else if(starts_with("/sum", uri))         {if(debug_on)printf("3\n"); serve_sum(fd, query);}      
      else                                      {if(debug_on)printf("4\n"); serve_request(fd, query);}
    }
      /* Clean up */
      free_dictionary(query);
      free_dictionary(headers);
  }
    /* Clean up status line */
    free(method);
    free(uri);
    free(version); //arb
}

/*
 * read_requesthdrs - read HTTP request headers
 */
dictionary_t *read_requesthdrs(rio_t *rp){
  char buf[MAXLINE];
  dictionary_t *d = make_dictionary(COMPARE_CASE_INSENS, free);

  Rio_readlineb(rp, buf, MAXLINE);
  printf("%s", buf);
  while(strcmp(buf, "\r\n")) {
    Rio_readlineb(rp, buf, MAXLINE);
    printf("%s", buf);
    parse_header_line(buf, d);
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
                          "Server: Friendlist Web Server\r\n",
                          "Connection: close\r\n",
                          "Content-length: ", len_str = to_string(len), "\r\n",
                          "Content-type: ", content_type, "\r\n\r\n",
                          NULL);
  free(len_str);

  return header;
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

static void serve_introduce(int fd, dictionary_t *query){


}

static void serve_befriend(int fd, dictionary_t *query){
  if(debug_on)printf("Enter Befriend\n");  

  char* body, *header;
  size_t len, i;

  char** keys = (char**)dictionary_keys(query);
  char** temp = keys;
  for(i = 0; temp[i] != NULL; i++)
    printf("keys %s\n", temp[i]);

  void* u_v = dictionary_get(query, "user");
  printf("u_v = %s\n",(char*)u_v);

  void* f_v = dictionary_get(query, "friends");
  printf("f_v = %s\n",(char*)f_v);
  char** f_v_split = split_string((char*)f_v, '\n');

  for(i = 0; f_v_split[i] != NULL; i++)
    printf("split_f_v %s\n", f_v_split[i]);

  const char* usr_name = (const char*)u_v;
  dictionary_t* new_d = dictionary_get(d, usr_name);  

  if(new_d == NULL) {
    printf("new_d NULL\n");
    new_d = make_dictionary(0, free);
    for(i = 0; f_v_split[i] != NULL; i++){
      printf("split_f_v %s\n", f_v_split[i]);
      printf("before: new_d count = %zu\n", new_d->count);
      dictionary_set(new_d, f_v_split[i], NULL);
      printf("after: new_d count = %zu\n", new_d->count);      
    }
    dictionary_set(d, usr_name, new_d);
  }else{
    printf("new_d __not__ NULL\n");
    printf("Count new_d = %zu\n", new_d->count);
    for(i = 0; f_v_split[i] != NULL; i++){
      printf("split_f_v %s\n", f_v_split[i]);
      if(contains(new_d, f_v_split[i]) < 0){
        printf("new_d does not contain\nnew_d->count before: %zu", new_d->count);
        dictionary_set(new_d, f_v_split[i], NULL);
        printf("new_d->count after: %zu", new_d->count);
      }else{
        printf("Already contained %s\n", f_v_split[i]);        
      }
    }
  }
  
  char** final_keys = (char**)dictionary_keys(d);
  for(i = 0; final_keys[i] != NULL; i++){
     printf("final_keys %s\n", final_keys[i]);
  }

  dictionary_t* check = dictionary_get(d, usr_name);
  printf("count check = %zu: \n", check->count);
  char** friend_name_keys = (char**)dictionary_keys(check);
  for(i = 0; friend_name_keys[i] != NULL; i++){
     printf("check_keys %s\n", friend_name_keys[i]);
  }

  char* final_str = join_strings((const char* const*) friend_name_keys, '\n');
  body = strdup(final_str);
  len = strlen(body);

  printf("All friends:\n %s\n", final_str);
  free(final_str);

  /* Send response headers to client */
  header = ok_header(len, "text/html; charset=utf-8");
  if(debug_on)printf("OK header\n");      
  Rio_writen(fd, header, strlen(header));
  printf("Response headers:\n");
  printf("%s", header);
  free(header);

  /* Send response body to client */
  Rio_writen(fd, body, len);
  if(debug_on)printf("RIO Writen\n");          
  free(body);
  if(debug_on)printf("exit\n");          
}

static int contains(dictionary_t* d, char* given_key)
{
  char** keys = (char**)dictionary_keys(d);
  int i;
  for(i = 0; keys[i] != NULL; i++){
    printf("contains keys %s\n", keys[i]);  
    if(strcmp(given_key, keys[i]) == 0){
      return i;
      printf("Match at index %d\n", i);
    }
  }
  return -1;
}

static void serve_friends(int fd, dictionary_t *query){

  size_t len = 0;
  char* body, *header, *usr_str = (char*)dictionary_get(query, "user"); /* Sometimes this will return NULL!!!! */
  //printf("usr_str = %s\n", usr_str);
  //printf("usr_str = %s\n", usr_str); /* debug only */


  dictionary_t* usr_d = (char*)dictionary_get(d, usr_str);
  if(usr_d != NULL)   {printf("usr_d->count = %zu\n", usr_d->count);} else {printf("usr_d is null\n"); return;}

  int i;
  char** usr_keys = (char**)dictionary_keys(usr_d);
  for(i = 0; usr_keys[i] != NULL; i++)
    printf("d_keys %s\n", usr_keys[i]);

  body = (char*)join_strings((const char* const*)usr_keys, '\n');
  len = strlen(body);
  printf("full string: %s\n", body);

  /* Send response headers to client */
  header = ok_header(len, "text/html; charset=utf-8");
  if(debug_on)printf("OK header\n");      
  Rio_writen(fd, header, strlen(header));
  printf("Response headers:\n");
  printf("%s", header);
  free(header);

  /* Send response body to client */
  Rio_writen(fd, body, len);
  if(debug_on)printf("RIO Writen\n");          
  free(body);
  if(debug_on)printf("exit\n");   


}

static void serve_unfriend(int fd, dictionary_t *query){

  size_t len = 0;
  char* body, *header, *usr_str = (char*)dictionary_get(query, "user"); /* Sometimes this will return NULL!!!! */
  char* frd_str = (char*)dictionary_get(query, "friends"); 


  //printf("usr_str = %s\n", usr_str);
  //printf("usr_str = %s\n", usr_str); /* debug only */

  dictionary_t* usr_d = (char*)dictionary_get(d, usr_str);
  if(usr_d != NULL)   {printf("usr_d->count = %zu\n", usr_d->count);} else {printf("usr_d is null\n"); return;}

  dictionary_remove(usr_d, frd_str);
  printf("\n %s should have been removed!!!\n", frd_str);

  int i;
  char** usr_keys = (char**)dictionary_keys(usr_d);
  for(i = 0; usr_keys[i] != NULL; i++)
    printf("d_keys %s\n", usr_keys[i]);

  body = (char*)join_strings((const char* const*)usr_keys, '\n');
  len = strlen(body);
  printf("full string: %s\n", body);

  /* Send response headers to client */
  header = ok_header(len, "text/html; charset=utf-8");
  if(debug_on)printf("OK header\n");      
  Rio_writen(fd, header, strlen(header));
  printf("Response headers:\n");
  printf("%s", header);
  free(header);

  /* Send response body to client */
  Rio_writen(fd, body, len);
  if(debug_on)printf("RIO Writen\n");          
  free(body);
  if(debug_on)printf("exit\n"); 

}


/*
 * serve_request - example request handler
 */
static void serve_request(int fd, dictionary_t *query)
{
  size_t len;
  char *body, *header;
  
  body = strdup("alice\nbob");

  len = strlen(body);

  /* Send response headers to client */
  header = ok_header(len, "text/html; charset=utf-8");
  Rio_writen(fd, header, strlen(header));
  printf("Response headers:\n");
  printf("%s", header);

  free(header);

  /* Send response body to client */
  Rio_writen(fd, body, len);

  free(body);
}

/*
 * clienterror - returns an error message to the client
 */
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg) 
{
  size_t len;
  char *header, *body, *len_str;

  body = append_strings("<html><title>Friendlist Error</title>",
                        "<body bgcolor=""ffffff"">\r\n",
                        errnum, " ", shortmsg,
                        "<p>", longmsg, ": ", cause,
                        "<hr><em>Friendlist Server</em>\r\n",
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
  int i, count;

  count = dictionary_count(d);
  for (i = 0; i < count; i++) {
    if(dictionary_value(d, i) != NULL)
    printf("%s=%s\n", dictionary_key(d, i), (const char *)dictionary_value(d, i));
    else
    printf("%s= NULL (print NULL)\n", dictionary_key(d, i));      
  }
  printf("\n");
}