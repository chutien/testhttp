#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include "err.h"

#define BSIZE 4096

int Sock;
char Buffer[BSIZE + 1];
size_t Bi;
size_t Blen;


size_t findchar(char *s, size_t len, char c) {
  size_t i = 0;
  while (i < len && s[i] != c)
    ++i;
  return i;
}


void putbuf(const char *mes, size_t len) {
  size_t i;
  for (i = 0; i < len; ++i) {
    if (Bi == BSIZE) {
      if (write(Sock, Buffer, BSIZE) < 0)
	syserr("write");
      Bi = 0;
    }
    Buffer[Bi] = mes[i];
    ++(Bi);
  }
  if (Bi == BSIZE) {
    if (write(Sock, Buffer, BSIZE) < 0)
      syserr("write");
    Bi = 0;
  }
}


void putbuf_requestline(char *httpaddr) {
  putbuf("GET ", 4);
  putbuf(httpaddr, strlen(httpaddr));
  putbuf(" HTTP/1.1\r\n", 11);
}


void putbuf_host(char *httpaddr) {
  size_t i = 0;
  if (memcmp(httpaddr, "http", 4) != 0)
    fatal("given test address is incorrect");
  i += 4;
  if (httpaddr[4] == 's')
    i += 1;
  if (memcmp(httpaddr + i, "://", 3) != 0)
    fatal("given test address is incorrect");
  i += 3;
  size_t len = strlen(httpaddr);
  size_t j = i;
  while (j < len && httpaddr[j] != '/')
    ++j;
  
  putbuf("Host:", 5);
  putbuf(httpaddr + i, j - i);
  putbuf("\r\n", 2);
}


int putbuf_cookie(FILE *fp) {
  do {
    char *rp = fgets(Buffer + Bi, BSIZE - Bi, fp);
    if (!rp) {
      if (feof(fp))
	return -1;
      else
	syserr("fgets");
    }
    Bi += strlen(Buffer + Bi);
    if (BSIZE - Bi < 2) {
      int rc = write(Sock, Buffer, BSIZE - 1);
      if (rc < 0)
	syserr("write");
      Bi = 0;
    }
  } while (Buffer[Bi - 1] != '\n');
  --Bi;
  return 0;
}


void putbuf_cookies(char *fname) {
  FILE *fp;
  fp = fopen(fname, "r");

  int c = fgetc(fp);
  if (ferror(fp))
    syserr("fgetc");
  else if (feof(fp))
    return;

  char head[9] = "Cookie:";
  head[7] = (char)c;

  putbuf(head, 8);
  putbuf_cookie(fp);
  do {
    if (BSIZE - Bi < 2) {
      if (write(Sock, Buffer, BSIZE - 1) < 0)
	syserr("write");
      Bi = 0;
    }
    Buffer[(Bi)++] = ';';
  } while (putbuf_cookie(fp) == 0);
  --(Bi);
  putbuf("\r\n", 2);
  
  if (fclose(fp) != 0)
    syserr("fclose");
}


void putbuf_end() {
  putbuf("\r\n", 2);
  if (Bi == 0)
    return;
  if (write(Sock, Buffer, Bi) < 0)
    syserr("write");
  Bi = 0;
}


void getbuf() {
  if (Bi < Blen)
    return;
  Bi = 0;
  Blen = read(Sock, Buffer, BSIZE);
  if (Blen < 0)
    syserr("read");
}


int getbuf_char() {
  getbuf();
  if (Blen == 0)
      return 0;
  return Buffer[Bi++];
}


void ungetbuf_char() {
  if (Bi > 0)
    --Bi;
  else
    fatal("Cannot put char back anymore");
}


void getbuf_ignore_until(char c) {
  getbuf();
  if (Blen == 0)
    fatal("unexpected response");
  size_t d;
  while ((d = findchar(Buffer + Bi, Blen - Bi, c)) == Blen - Bi) {
    Bi = Blen;
    getbuf();
  }
  Bi += d + 1;
}


void getbuf_ignore_line() {
  getbuf_ignore_until('\r');
  if (getbuf_char() != '\n')
    fatal("unexpected response");
  //  else
  //  ungetbuf_char();
}

// TODO
/* void getbuf_print_until(char delimiter) { */
/*   if (Bi == Blen) { */
/*     Bi = 0; */
/*     Blen = read(Sock, Buffer, BSIZE); */
/*     if (Blen < 0) */
/*       syserr("read"); */
/*     if (Blen == 0) */
/*       fatal("unexpected response"); */
/*   } */
/*   size_t d;  */
/*   while ((d = findchar(Buffer + Bi, Blen, delimiter)) < 0) { */
/*     write(1, Buffer + Bi, Blen); */
/*     Bi = 0; */
/*     if ((Blen = read(Sock, Buffer, BSIZE)) < 0) */
/*       syserr("read"); */
/*     if (Blen == 0) */
/*       fatal("unexpected response"); */
/*   } */
/*   if (write(1, Buffer + Bi, Bi + d) < 0) */
/*     syserr("write"); */
/*   if (putchar('\n') == EOF) */
/*     syserr("putchar"); */
/*   ++Bi; //TODO */
/* } */


int getbuf_statuscode() {
  static const char *httpver = "HTTP/1.1 ";
  
  for (size_t i = 0; i < strlen(httpver); ++i) {
    int c = getbuf_char();
    if (Blen == 0 || c != httpver[i])
      fatal("unexpected response");
  }
  
  char status_code[4];
  for (size_t i = 0; i < 3; ++i) {
    status_code[i] = getbuf_char();
    if (Blen == 0 || !isdigit(status_code[i]))
      fatal("unexpected response");
  }
  
  int c = getbuf_char();
  if (Blen == 0 || !isspace(c))
    fatal("unexpected response");

  status_code[3] = '\0';
  return atoi(status_code);
}


int istchar(int c) {
  static const char other[] = {'!', '#', '$', '%', '&', '\'', '*', '+', '-', '.', '^', '_', '`', '|', '~'};
  static const size_t len = 15;
  if (isalnum(c))
    return 1;
  size_t i;
  for (i = 0; i < len; ++i) {
    if (other[i] == c)
      return 1;
  }
  return 0;
}


int isqdtext(int c) {
  if (isblank(c) || c == 0x21 || (0x23 <= c && c <= 0x27) || (0x2AA <= c && c <= 0x5B) || (0x5D <= c && c <= 0x7E)
      || (0x80 <= c && c <= 0xFF))
    return 1;
  else
    return 0;
}


int isquotedpair(int c1, int c2) {
  if (c1 != '\\')
    return 0;
  if (isblank(c2) || (0x80 <= c2 && c2 <= 0xFF) || isprint(c2))
    return 1;
  else
    return 0;
}


void getbuf_ows() {
  int c;
  do {
    c = getbuf_char();
    if (Blen == 0)
      fatal("unexpected response");
  } while (isblank(c));
  ungetbuf_char();
}


void getbuf_printif(int (*f)(int)) {
  do {
    getbuf();
    if (Blen == 0)
      fatal("unexpected response");
    size_t st = Bi;
    while (Bi < Blen && (*f)(Buffer[Bi]))
      ++Bi;
    write(1, Buffer + st, Bi - st);
  } while (Bi == Blen);
}


void getbuf_cookie() {
  getbuf_printif(istchar);

  getbuf_ows();

  if (getbuf_char() != '=')
    fatal("unexpected response");
  putchar('=');
  
  getbuf_ows();

  char c = getbuf_char();
  int isqstr = (c == '"');

  if (!isqstr) {
    if (!istchar(c))
      fatal("unexpected response");
    putchar(c);
    getbuf_printif(istchar);
  } else {
    putchar('"');
    for (;;) {
      getbuf_printif(isqdtext);
      int c1 = getbuf_char();
      if (c1 != '\\') {
        if (c1 != '"')
	  fatal("unexpected response");
	putchar('"');
	break;
      }
      int c2 = getbuf_char();
      if (!isquotedpair(c1, c2))
	putchar(c2);
      else
        fatal("unexpected response");
    }
  }

  getbuf_ows();
  getbuf_ignore_line();
}

size_t getbuf_header(const char *fields[], size_t fsize) {
  int c = getbuf_char();
  if (c == '\r') {
    if (getbuf_char() == '\n')
      return fsize + 1;
    else
      fatal("unexpected response");
  }

  size_t beg = 0;
  size_t end = fsize;
  size_t i = 0;
  while (beg + 1 < end && istchar(c)) {
    c = tolower(c);
    
    size_t p = beg;
    size_t k = end;
    while (p < k) {
      size_t s = p + (k - p) / 2;
      if (fields[s][i] < c)
	p = s + 1;
      else
	k = s;
    }
    
    beg = p;
    k = end;
    while (p < k) {
      size_t s = p + (k - p) / 2;
      if (fields[s][i] > c)
	k = s;
      else
	p = s + 1;
    }
    end = k;
    c = getbuf_char();
    ++i;
    fprintf(stderr, "\nbeg: %zu, end: %zu\n", beg, end);
  }

  if (beg + 1 == end) {
    int ok = 1;
    size_t flen = strlen(fields[beg]);
    fprintf(stderr, "\n%zu %c\n", i, c);
    for (; i < flen; ++i) {
      c = tolower(c);
      if (c != fields[beg][i]) {
	ok = 0;
	break;
      }
      c = getbuf_char();
    }
    if (ok && c == ':') {
      getbuf_ows();
      return beg;
    }
  }
  
  getbuf_ignore_until(':');
  getbuf_ows();
  return fsize;
}


void getbuf_headers() {
  static const char *fields[] = {"content-length", "set-cookie", "transfer-encoding"};
  static const size_t fsize = 3;
  
  size_t i;
  while ((i = getbuf_header(fields, fsize)) <= fsize) {
    fprintf(stderr, "\nLOGGING: c: %c, Bi: %zu, Blen: %zu, i: %zu\n", Buffer[Bi], Bi, Blen, i);
    switch (i) {
    case 0: // content-length
      break;

    case 1: // set-cookie
      getbuf_cookie();
      break;

    case 2: // transfer-encoding
      break;

    default:
      getbuf_ignore_line();
    }
  }
}


int main(int argc, char *argv[]) {
  int rc;
  
  if (argc != 4)
    syserr("Usage: %s <address>:<port> <cookies file> <test address>", argv[0]);

  Sock = socket(PF_INET, SOCK_STREAM, 0);
  if (Sock < 0)
    syserr("socket");

  struct addrinfo addr_hints, *addr_results;
  memset(&addr_hints, 0, sizeof(struct addrinfo));
  addr_hints.ai_flags = 0;
  addr_hints.ai_family = AF_INET;
  addr_hints.ai_socktype = SOCK_STREAM;
  addr_hints.ai_protocol = IPPROTO_TCP;

  size_t d = findchar(argv[1], strlen(argv[1]), ':');
  argv[1][d] = '\0';
  char *ser_addr = argv[1];
  char *ser_port = argv[1] + d + 1;
  
  rc = getaddrinfo(ser_addr, ser_port, &addr_hints, &addr_results);
  if (rc != 0)
    syserr("getaddrinfo");

  rc = connect(Sock, addr_results->ai_addr, addr_results->ai_addrlen);
  if (rc != 0)
    syserr("connect");

  freeaddrinfo(addr_results);

  Bi = 0;

  putbuf_requestline(argv[3]);
  putbuf_host(argv[3]);
  putbuf("Connection:close\r\n", 18);
  putbuf_cookies(argv[2]);
  putbuf_end();

  Bi = 0;
  Blen = 0;
  
  int status_code = getbuf_statuscode();
  if (status_code != 200) {
    getbuf_printif(isprint);
    putchar('\n');
  } else {
    getbuf_ignore_line();
    getbuf_headers();
  }
  
  if (close(Sock) < 0)
    syserr("close");
  
  return 0;
}
