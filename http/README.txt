Start with the server homework handout:

 http://www.eng.utah.edu/~cs4400/servlab-handout.zip

If you `make` and then run

   ./redpin 8090

then you can connect to it with

   curl http://localhost:8090

and you'll get the response "alice", a newline, "bob", and a newline.

You could also connect to "http://localhost:8090" using a web browser,
but we'll stick to `curl`.


Part 1
------

Adjust the initial server to recognize a "/sum" request and return
"79\n" instead of "alice\nbob\n".

  curl "http://localhost:8090/sum"

should report

 79

while any other path, such as

  curl "http://localhost:8090/other"

still reports "alice\nbob\n".

Hints:

 * Make a function like `serve_request`, but call it `serve_sum`.

 * Call `serve_sum` instead of `serve_request` when the `uri`
   string starts with "/sum".

 * You can use `starts_with` from "more_string.[ch]" to check whether
   `uri` starts with "/sum".


The answer is provided in diff form in "pt1.diff". The `patch` command
can apply that diff to "redpin.c".


Part 2
------

Adjust the "/sum" handler to recognize "x" and "y" query arguments and
report the sum of those numbers if "x" and "y" are both provided,
instead of always reporting "79".

For example,

  curl "http://localhost:8090/sum?x=40&y=2"

should report

 42

with a newline, while

  curl "http://localhost:8090/sum?x=40"

should report an error that includes the text

 Please provide numbers

since no "y" query argument is provided.

(Ideally, that error is reported with a 400 HTTP status, which
indicates a bad request, but you should skip that refinement.)


Hints:

 * Change `serve_request`.

 * The "x" and "y" query parameters can be extracted from the `query`
   argument using the dictionary functions defined in "dictionary.c".

 * Use `atoi` (from the C library), and `to_string` (from
   "more_string.[ch]"), and `append_strings` (from
   "more_string.[ch]").

 * Be mindful of string allocation to avoid creating a memory leak.

 * If you want to return a 400 status, the `clienterror` function is
   useful.


The answer is provided in diff form in "pt2.diff". It applies to a
"redpin.c" that is already patched with "pt1.diff".


Part 3
------

If you add

    Sleep(30)

to the part of your server that adds "x" and "y" arguments when both
are given, then requests that don't include an "x" or "y" argument
should be fast, but a request with "x" and "y" arguemnts should take
30 seconds to respond. Worse, the slow requests will block all new
(potentially fast) requests.

Try adding that and check that

  curl "http://localhost:8090/sum?x=40"

complains quickly, while

  curl "http://localhost:8090/sum?x=40&y=2"

takes 30 seconds to respond.

Leave the `Sleep` call in, but adjust your server so that it handles
each connection concurrently. That way, while a request with "x" and
"y" has to wait 30 seconds, a request without "x" or "y" can be
handled immediately.


Hints:

 * Use `Pthread_create` and a function that is called in a new thread
   and wraps `doit`.

 * Since connections are independent, you won't need a semaphore.


The answer is provided in diff form in "pt3.diff". It applies to a
"redpin.c" that is already patched with "pt1.diff" and "pt2.diff".
