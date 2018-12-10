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
void *doit_thread(void* connfd_p);
static void serve_counts(int fd);
static void serve_pin(int fd, dictionary_t *query);
static void serve_reset(int fd);
static void serve_people(int fd, dictionary_t *query);
static void serve_places(int fd, dictionary_t *query);
static void serve_unpin(int fd, dictionary_t *query);
void print_inner(dictionary_t* d);
static char *get_person_header(char* place, char* host, char* port);
static char *get_place_header(char* person, char* host, char* port); 
static void serve_copy(int fd, dictionary_t *query);
void serve_print_all(int fd, dictionary_t *query);


/* TO DO 
*  1) do you remove correctly on unpin
*  2) fixing seg faults on places?person where person doesn't exist
*/

dictionary_t* d_ppl;
dictionary_t* d_plcs;
int debug_on = 1;

int ppl_count = 0;
int places_count = 0;

char* current_serv_port; 

sem_t thread;
sem_t dict_sem;

int main(int argc, char **argv) 
{
  int listenfd, connfd;
  char hostname[MAXLINE], port[MAXLINE];
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;
  current_serv_port = argv[1];

  Sem_init(&thread, 0, 1);
  Sem_init(&dict_sem, 0, 1);

  d_ppl = make_dictionary(COMPARE_CASE_SENS, (free_proc_t)dictionary_free );
  d_plcs = make_dictionary(COMPARE_CASE_SENS, (free_proc_t)dictionary_free );

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

/*
sem_t
Sem_init(sem, ps_share, value)
P(sem)  lock()
V(sem) unlock()
*/

  /* Also, don't stop on broken connections: */
  /*In the case of a broken pipe which wasn't closed on one end, no error is thrown*/
  Signal(SIGPIPE, SIG_IGN);


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
      P(&thread);
      Pthread_detach(t1);
      /*Close(connfd); */ //old
    }
  }
}

void *doit_thread(void* connfd_p){
  int connfd = *(int*)connfd_p;
  free(connfd_p);
  doit(connfd);
  Close(connfd);
  return NULL;

}

/*
 * doit - handle one HTTP request/response transaction
 */
void doit(int fd) 
{

  V(&thread);
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

      /* Parse all query arguments into a dictionary */
      query = make_dictionary(COMPARE_CASE_SENS, free);
      parse_uriquery(uri, query);
      if (!strcasecmp(method, "POST"))
        read_postquery(&rio, headers, query);


      //detect_str(query, "alice");
      /* For debugging, print the dictionary */
      P(&dict_sem);
      if(starts_with("/counts", uri))           {if(debug_on)printf("counts\n"); serve_counts(fd);}
      else if(starts_with("/reset", uri))       {if(debug_on)printf("reset\n"); serve_reset(fd);}
      else if(starts_with("/people", uri))      {if(debug_on)printf("people\n"); serve_people(fd, query);}      
      else if(starts_with("/places", uri))      {if(debug_on)printf("places\n"); serve_places(fd, query);}
      else if(starts_with("/pin", uri))         {if(debug_on)printf("pin\n"); serve_pin(fd, query);}
      else if(starts_with("/unpin", uri))       {if(debug_on)printf("unpin\n"); serve_unpin(fd, query);}      
      else if(starts_with("/copy", uri))        {if(debug_on)printf("copy\n"); serve_copy(fd, query);}      
      else if(starts_with("/print_all", uri))   {if(debug_on)printf("print_all\n"); serve_print_all(fd, query);}            
      else                                      {if(debug_on)printf("ERROR\n"); clienterror(fd, "?", "400", "BAD", "REALLY BAD");}
      V(&dict_sem);

      /*
      void clienterror(int fd, char *cause, char *errnum, 
     char *shortmsg, char *longmsg) 
{
 */

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

void serve_print_all(int fd, dictionary_t *query)
{

  if(!debug_on)
    return;

  printf("*********** PEOPLE DICTIONARY *********\n");
  print_stringdictionary(query);

  printf("------------------ PEOPLE DICTIONARY --------------------\n");
  print_inner(d_ppl);
  printf("__________________ PLACES DICTIONARY ____________________\n");  
  print_inner(d_plcs);


}

void print_inner(dictionary_t* d)
{
  printf("PRINT INNER\n");
  const char** keys = dictionary_keys(d);
  dictionary_t* tmp;
  const char** inner_keys = NULL;
  int i;
  int k;
  for(i = 0; keys[i] != NULL; i++){
    printf("OUTER KEY %s HAS:\n", keys[i]);
    tmp = (dictionary_t*)dictionary_get(d, keys[i]);
    inner_keys = dictionary_keys(tmp);
    for(k = 0; inner_keys[k] != NULL; k++)
      printf("\t%s\n", inner_keys[k]);
  }

  if(inner_keys)
    free(inner_keys);
  if(keys)
    free(keys);

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


static void serve_counts(int fd)
{
  size_t len;
  char *body, *header;

  //print_inner(d_ppl);

  char* ppl_cnt = to_string(dictionary_count(d_ppl));
  char* plcs_cnt = to_string(dictionary_count(d_plcs));  


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

static void serve_people(int fd, dictionary_t *query)
{
  size_t len;
  char *body, *header;
  char* place;
  const char** people_arr = NULL;

  place = dictionary_get(query, "place");
  printf("place assigned\n");
  if(place){
    printf("place-> %s\n", place);    
    if(dictionary_has_key(d_plcs, place)){
    people_arr = dictionary_keys((dictionary_t*)dictionary_get(d_plcs, place));
    body = join_strings(people_arr, '\n');
    }else
    body = strdup("");
  }else{
    people_arr = dictionary_keys(d_ppl);
    body = join_strings(people_arr, '\n');
  }

  if(people_arr)
    free(people_arr);

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

static void serve_places(int fd, dictionary_t *query)
{
  size_t len;
  char *body, *header;
  char* person;
  const char** places_arr = NULL;
  
  person = dictionary_get(query, "person");
  printf("person-> %s\n", person);    
  if(person){
    if(dictionary_has_key(d_ppl, person)){
      places_arr = dictionary_keys((dictionary_t*)dictionary_get(d_ppl, person));
      body = join_strings(places_arr, '\n');
    }else
    body = strdup("");

  }else{

    places_arr = dictionary_keys(d_plcs);
    body = join_strings(places_arr, '\n');
  }

  len = strlen(body);
  free(places_arr);

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


static void serve_reset(int fd)
{
  dictionary_free(d_ppl);
  d_ppl = make_dictionary(COMPARE_CASE_SENS, (free_proc_t)dictionary_free);
  d_plcs = make_dictionary(COMPARE_CASE_SENS, (free_proc_t)dictionary_free);
  serve_counts(fd);
}

static void serve_pin(int fd, dictionary_t *query)
{




  int i = 0;
  char* people_dict;
  char** people_arr;


  char* places_dict;
  char** places_arr;


  if(dictionary_get(query, "people")){
    people_dict = dictionary_get(query, "people");
    people_arr = split_string((const char*)people_dict, '\n');
  }else{
    clienterror(fd, "?","400", "Bad Request", "people on pin");
    return;
  }



  if(dictionary_get(query, "places")){
    places_dict = dictionary_get(query, "places");
    places_arr = split_string((const char*)places_dict, '\n');  
  } else{
    clienterror(fd, "?","400", "Bad Request", "places on pin");
    return;
  }  



/*
  char **people_arr, **places_arr;  

  char *people = dictionary_get(query, "people");
  if (people){
    people_arr = split_string(people, '\n');
  }

  char *places = dictionary_get(query, "places");
  if (places){
    places_arr = split_string(people, '\n');
  }
  else{
    clienterror(fd, "?","400", "Bad Request", "places on pin\n");
    return;
  }
  */





  dictionary_t* tmp_prsn_dict;
  dictionary_t* tmp_plcs_dict;

  int k = 0;
  for(i = 0; people_arr[i] != NULL; i++){  
      for(k = 0; places_arr[k] != NULL; k++){  
        if(dictionary_has_key(d_ppl, people_arr[i]))
        { dictionary_set(dictionary_get(d_ppl, people_arr[i]), places_arr[k], NULL);} //connect existing person to place
        else{
          tmp_prsn_dict = make_dictionary(COMPARE_CASE_SENS, NULL);
          dictionary_set(tmp_prsn_dict, places_arr[k], NULL);   // connect new person to place
          dictionary_set(d_ppl, people_arr[i], tmp_prsn_dict);
        }

        if(dictionary_has_key(d_plcs , places_arr[k]))
        { dictionary_set(dictionary_get(d_plcs, places_arr[k]), people_arr[i], NULL);}
        else{
          tmp_plcs_dict = make_dictionary(COMPARE_CASE_SENS, NULL);
          dictionary_set(tmp_plcs_dict, people_arr[i], NULL);
          dictionary_set(d_plcs, places_arr[k], tmp_plcs_dict);
        }
      }
  }

  for(i = 0; people_arr[i] != NULL; i++){  
    printf("removing people KEYS[%i]: %s\n", i , people_arr[i]);
    free(people_arr[i]);
  }

  free(people_arr);

  //free(people_dict);

  for(i = 0; places_arr[i] != NULL; i++){  
    printf("removing place KEYS[%i]: %s\n", i , places_arr[i]);
    free(places_arr[i]);
  }

  free(places_arr);

  //free(places_dict);

  printf("AFTER\n");  

  print_stringdictionary(d_ppl);
  print_stringdictionary(d_plcs); 


  if(debug_on)serve_print_all(fd, query);
  serve_counts(fd);


  //free(body);
}




static void serve_unpin(int fd, dictionary_t *query)
{

  printf("ENTERED QUERY:\n");
  print_stringdictionary(query);
  
  int i = 0;

  char* people_dict;
  char** people_arr;

  char* places_dict;
  char** places_arr;

  if(dictionary_get(query, "people")){
    people_dict = dictionary_get(query, "people");
    people_arr = split_string((const char*)people_dict, '\n');
  }else{
      clienterror(fd, "?","400", "Bad Request", "people on unpin");
      return;
  }  

  if(dictionary_get(query, "places")){
    places_dict = dictionary_get(query, "places");
    places_arr = split_string((const char*)places_dict, '\n');  
  }else{
      clienterror(fd, "?","400", "Bad Request", "person on pin");
      return;
  }

  print_stringdictionary(d_ppl);
  print_stringdictionary(d_plcs);  

  int k = 0;
  for(i = 0; people_arr[i] != NULL; i++){  
      for(k = 0; places_arr[k] != NULL; k++){  
        if(dictionary_has_key(d_ppl, people_arr[i])){
          dictionary_remove(dictionary_get(d_ppl, people_arr[i]), places_arr[k]);
          if(dictionary_count(dictionary_get(d_ppl, people_arr[i])) == 0){
            dictionary_remove(d_ppl, people_arr[i]);
            //dictionary_free(dictionary_get(d_ppl, people_arr[i]));
          }

        }

        if(dictionary_has_key(d_plcs , places_arr[k])){
          dictionary_remove(dictionary_get(d_plcs, places_arr[k]), people_arr[i]);
          if(dictionary_count(dictionary_get(d_plcs, places_arr[k])) == 0){
            dictionary_remove(d_plcs, places_arr[k]);
            //dictionary_free(dictionary_get(d_plcs, places_arr[k]));
          }
        }
      }
  }

  for(i = 0; people_arr[i] != NULL; i++){  
    printf("removing people KEYS[%i]: %s\n", i , people_arr[i]);
    free(people_arr[i]);
  }

  free(people_arr);
  //free(people_dict);

  for(i = 0; places_arr[i] != NULL; i++){  
    printf("removing place KEYS[%i]: %s\n", i , places_arr[i]);
    free(places_arr[i]);
  }

  free(places_arr);
  //free(places_dict);


  print_stringdictionary(d_ppl);
  print_stringdictionary(d_plcs);  
  if(debug_on)serve_print_all(fd, query);
  serve_counts(fd);
  //free(body);
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



static void serve_copy(int fd, dictionary_t *query)
{

  char *hostname;
  char *portno;
  char* target_str;

  if(dictionary_get(query, "host")){
    hostname = dictionary_get(query,"host");
  }
  else{
    clienterror(fd, "?","400", "Bad Request", "host on cpy");
    return;
  }


  if(dictionary_get(query, "port")){
    portno = dictionary_get(query,"port");
  }
  else{
    clienterror(fd, "?","400", "Bad Request", "port on cpy");
    return;
  }

  
  if(dictionary_get(query, "as")){
    target_str = dictionary_get(query,"as");
  }
  else{
    clienterror(fd, "?","400", "Bad Request", "as on cpy");
    return;
  }


  int is_place = 0;
  if(dictionary_get(query, "person") == NULL)
    is_place = 1;

  printf("1\n");
  // Get addressing information
  struct addrinfo hints, *addrs, *res;

  memset(&hints, 0, sizeof(struct addrinfo));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  Getaddrinfo(NULL, current_serv_port, &hints, &addrs);

  struct sockaddr_in *ipv4;
  char current_serv_ip[INET_ADDRSTRLEN];
  char host[256];

  printf("2\n");

  // Check whether the host and port in the introduce query match the current server's ipv4 information
  int is_current_server = 0;
  for (res = addrs; res != NULL; res = res->ai_next) {
    ipv4 = (struct sockaddr_in *)res->ai_addr;
    inet_ntop(AF_INET, &(ipv4->sin_addr), current_serv_ip, INET_ADDRSTRLEN);

    if (!strcmp(hostname, current_serv_ip) && !strcmp(portno, current_serv_port))
      is_current_server = 1;

    Getnameinfo(res->ai_addr, res->ai_addrlen,
                host, sizeof(host),
                NULL, 0,
                NI_NUMERICSERV);    

    if (!strcmp(hostname, host) && !strcmp(portno, current_serv_port))
      is_current_server = 1;
  }

  printf("3\n");  

  Freeaddrinfo(addrs);
  Freeaddrinfo(res);

  // if it is the current server...
  if (is_current_server) {

    //GET the person with places to be transfered

    char* person_with_places = dictionary_get(query, "person");
    char* place_with_people = dictionary_get(query, "place");   
    char* target_person = target_str;
    char* target_place = target_str;
    dictionary_t* new_tar_dict;


    //char* lcl_to_rmt_header = NULL;
    int i;
    const char** persons_places_keys = NULL;
    const char** places_people_keys = NULL;
    const char** tar_keys = NULL;



    new_tar_dict = make_dictionary(COMPARE_CASE_SENS, NULL);

    printf("1\n");

    if(is_place)
    {
      printf("10\n");      
      places_people_keys = dictionary_keys(dictionary_get(d_plcs, place_with_people));
      printf("11\n");
      for(i = 0; places_people_keys[i] != NULL; i++){
        dictionary_set(new_tar_dict, places_people_keys[i], NULL);
        printf("12\n");
        dictionary_set(dictionary_get(d_ppl, places_people_keys[i]),target_place, NULL);        
        printf("13\n");        
      }

        printf("14\n");
      if(dictionary_has_key(d_plcs,target_place)){
        printf("15\n");        
        for(i = 0;  tar_keys[i] != NULL; i++){
          dictionary_set(new_tar_dict, target_place, NULL);
          printf("16\n");                  
        }
        printf("17\n");        
      }
      printf("18\n");              
          
      dictionary_set(d_plcs, target_place, new_tar_dict);

    }else{

      printf("a\n");
      persons_places_keys = dictionary_keys(dictionary_get(d_ppl, person_with_places));
      printf("b\n");      
      for(i = 0; persons_places_keys[i] != NULL; i++){
        printf("b2\n");              
        dictionary_set(new_tar_dict, persons_places_keys[i], NULL);
        printf("c\n");              
        dictionary_set(dictionary_get(d_plcs, persons_places_keys[i]), target_person, NULL);        
        printf("d\n");                      
      }

        printf("f\n");                      
      if(dictionary_has_key(d_ppl,target_person)){
        for(i = 0;  tar_keys[i] != NULL; i++){
        printf("g\n");                                
          dictionary_set(new_tar_dict, target_person, NULL);
        printf("h\n");                                
        }
      }
         
        printf("i\n");                                
        dictionary_set(d_ppl, target_person, new_tar_dict);
        printf("j\n");                            

    }

    free(tar_keys);

    //lcl_to_rmt_header = get_people_header(NULL, NULL, NULL);
    serve_counts(fd);

  }
  // if not the current server...
  else {

    char* person_with_places_rem = dictionary_get(query, "person");
    char* place_with_people_rem = dictionary_get(query, "place");
    char* target_person_rem = target_str;
    char* target_place_rem = target_str;

    char *header;

    socklen_t s = Open_clientfd(hostname, portno);

    /* Don't kill the server if there's an error, because
       we want to survive errors due to a client. But we
      do want to report errors. */
    exit_on_error(0);

    /* Also, don't stop on broken connections: */
    Signal(SIGPIPE, SIG_IGN);


    if(is_place)
    {
      header = get_person_header(place_with_people_rem, hostname, portno);
    }else{
      header = get_place_header(person_with_places_rem, hostname, portno);      
    }


    Rio_writen(s, header, strlen(header));
    if(is_place)
      printf("place query headers sent:\n");
    else
      printf("person query headers sent\n");

    free(header);

    char buf[MAXLINE], *uri, *method, *version, *len_str;
    char *buffer;
    int len;
    rio_t rio;
    dictionary_t *headers;

  /* Read response line and headers */
    Rio_readinitb(&rio, s);
    if (Rio_readlineb(&rio, buf, MAXLINE) <= 0)
      return;
    /* Print headers for debugging */
    printf("%s", buf);
    
    if (!parse_request_line(buf, &method, &uri, &version)) {
      clienterror(fd, method, "400", "Bad Request",
                  "Redpin did not recognize the request");
    } 
    else {
      /* Parse HTTP response header */
      headers = read_requesthdrs(&rio);

      len_str = dictionary_get(headers, "Content-Length");
      len = (len_str ? atoi((const char*)len_str) : 0);

      buffer = malloc(len+1);

      Rio_readnb(&rio, buffer, len);
      buffer[len] = 0;

      printf("BUFFER:\n%s\n", buffer);

      // GET BOBS PLACES
      char** persons_places = split_string(buffer, '\n');
      char** places_people = split_string(buffer, '\n');

      int y;
      dictionary_t* new_target_dict;
      dictionary_t* new_person_or_place;
      const char** targets_keys;

      new_target_dict = make_dictionary(COMPARE_CASE_SENS, NULL);       

      if(is_place)
      {
        for(y = 0; places_people[y] != NULL; y++){
          printf("\n\t&&& places_people[%i] =  %s\n", y, places_people[y]);
          dictionary_set(new_target_dict, places_people[y], NULL); //set target as place y
          if(!dictionary_has_key(d_ppl, places_people[y])){        //if people dictionary doesnt have place y's 
            new_person_or_place = make_dictionary(COMPARE_CASE_SENS, NULL);
            dictionary_set(new_person_or_place, target_place_rem, NULL);
            dictionary_set(d_ppl, places_people[y], new_person_or_place);
          }
        }

        if(dictionary_has_key(d_ppl, target_person_rem)){
          targets_keys = dictionary_keys(dictionary_get(d_plcs, target_place_rem));
          for(y = 0; targets_keys[y] != NULL; y++){
            printf("\n\t*** places_people[%i] =  %s\n", y, targets_keys[y]);              
            dictionary_set(new_target_dict, targets_keys[y], NULL);

          }
        }
        dictionary_set(d_plcs, target_place_rem, new_target_dict);


      }else
      {
        for(y = 0; persons_places[y] != NULL; y++){
          printf("\n\t&&& places_people[%i] =  %s\n", y, places_people[y]);          
          dictionary_set(new_target_dict, persons_places[y], NULL);
          if(!dictionary_has_key(d_plcs, persons_places[y])){
            new_person_or_place = make_dictionary(COMPARE_CASE_SENS, NULL);
            dictionary_set(new_person_or_place, target_person_rem, NULL);
            dictionary_set(d_plcs, persons_places[y], new_person_or_place);
          }
        }

        if(dictionary_has_key(d_plcs, target_person_rem)){
          targets_keys = dictionary_keys(dictionary_get(d_ppl, target_person_rem));
          for(y = 0; targets_keys[y] != NULL; y++){
            printf("\n\t*** places_people[%i] =  %s\n", y, targets_keys[y]);                          
            dictionary_set(new_target_dict, targets_keys[y], NULL);
          }
        }
        dictionary_set(d_ppl, target_person_rem, new_target_dict);

      }


      //  /Clean up
      free(len_str);
      //free(buffer);
      free(method); 
      free(uri);
      free(version);    

      if(debug_on)serve_print_all(fd, query);
      serve_counts(fd);

    }
  }
      printf("done with copy \n");  
}



/* 
* Prepare an HTTP GET header for an introduce query.
*/
static char *get_person_header(char* place, char* host, char* port) {
  char *header;

  header = append_strings("GET /people?place=", place, " HTTP/1.1\r\n",
                          "Host: ", host, ":", port, "\r\n",
                          "User-Agent: Redpin Web Server (net/http-client)\r\n",
                          "Content-Length: 0\r\n\r\n",
                          NULL);

  return header;
}

static char *get_place_header(char* person, char* host, char* port) {
  char *header;

  header = append_strings("GET /places?persone=", person, " HTTP/1.1\r\n",
                          "Host: ", host, ":", port, "\r\n",
                          "User-Agent: Redpin Web Server (net/http-client)\r\n",
                          "Content-Length: 0\r\n\r\n",
                          NULL);


  return header;
}










  









