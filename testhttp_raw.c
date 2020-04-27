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
#include <stdint.h>
#include "err.h"

#define BSIZE 4096

/* Buffer */
int Sock;
char Buffer[BSIZE + 1];
size_t Bi;
size_t Blen;

/* Headers */
int ChunkedIsLast = 0;

size_t hex_char(int c) {
  c = tolower(c);
  if ('a' <= c && c <= 'f')
    return c - 'a' + 10;
  else if ('0' <= c && c <= '9')
    return c - '0';
  else
    fatal("incorrect char");
  return 0;
}

void putchar_check(int c) {
  if (putchar(c) == EOF)
    syserr("putchar");
}

void ignore_char(int c) {
  return;
}

size_t findchar(char *s, size_t len, char c) {
  size_t i = 0;
  while (i < len && s[i] != c)
    ++i;
  return i;
}

int isnot_lf(int c) {
  if (c != '\n')
    return 1;
  else
    return 0;
}

int isnot_coma_cr(int c) {
  if (c != ',' && c != '\r')
    return 1;
  else
    return 0;
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

int isvchar(int c) {
  if (0x21 <= c && c <= 0x7E)
    return 1;
  else
    return 0;
}

int isobstext(int c) {
  if (0x80 <= c && c <= 0xFF)
    return 1;
  else
    return 0;
}

int is_blank_vchar_obs(int c) {
  if (isblank(c) || isvchar(c) || isobstext(c))
    return 1;
  else
    return 0;
}

int isqdtext(int c) {
  if (isblank(c) || c == 0x21 || (0x23 <= c && c <= 0x5B) || (0x5D <= c && c <= 0x7E)
      || isobstext(c))
    return 1;
  else
    return 0;
}

int isquotedpair(int c1, int c2) {
  if (c1 != '\\')
    return 0;
  if (is_blank_vchar_obs(c2))
    return 1;
  else
    return 0;
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

void putbuf_request(char *httpaddr) {
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
  if (!fp)
    syserr("fopen");
  
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
  ssize_t r = read(Sock, Buffer, BSIZE);
  if (r < 0)
    syserr("read");
  Blen = (size_t)r;
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

void getbuf_crlf() {
  int cr = getbuf_char();
  if (Blen == 0 || cr != '\r')
    fatal("unexpected response");
  int lf = getbuf_char();
  if (Blen == 0 || lf != '\n')
    fatal("unexpected response");
}

void getbuf_ignore(size_t n) {
  for (size_t i = 0; i < n; ++i) {
    getbuf_char();
    if (Blen == 0)
      fatal("unexpected error");
  }
}

size_t getbuf_ignore_until(char c) {
  getbuf();
  if (Blen == 0)
    fatal("unexpected response");
  size_t ignored = 0;
  size_t d;
  while ((d = findchar(Buffer + Bi, Blen - Bi, c)) == Blen - Bi) {
    ignored += d;
    Bi = Blen;
    getbuf();
  }
  Bi += d + 1;
  return ignored + d;
}

size_t getbuf_ignore_line() {
  size_t r = getbuf_ignore_until('\r');
  if (getbuf_char() != '\n')
    fatal("unexpected response");
  return r;
}

size_t getbuf_while(int (*f)(int), void (*g)(int)) {
  size_t r = 0;
  char c = getbuf_char();
  if (Blen == 0)
    fatal("unexpected response");
  while ((*f)(c)) {
    ++r;
    g(c);
    c = getbuf_char();
    if (Blen == 0)
      fatal("unexpected response");
  }
  ungetbuf_char();
  return r;
}

int getbuf_response_line_ok() {
  static const char *httpver = "HTTP/1.1 ";
  
  for (size_t i = 0; i < strlen(httpver); ++i) {
    int c = getbuf_char();
    if (Blen == 0 || c != httpver[i])
      fatal("unexpected response");
  }
  
  char status_code[3];
  for (size_t i = 0; i < 3; ++i) {
    status_code[i] = getbuf_char();
    if (Blen == 0 || !isdigit(status_code[i]))
      fatal("unexpected response");
  }
  
  int c = getbuf_char();
  if (Blen == 0 || !isspace(c))
    fatal("unexpected response");

  if (memcmp(status_code, "200", 3) != 0) {
    for (size_t i = 0; i < 3; ++i)
      putchar(status_code[i]);
    putchar(' ');
    getbuf_while(is_blank_vchar_obs, putchar_check);
    putchar('\n');
    getbuf_ignore_line();
    return 0;
  } else {
    getbuf_ignore_line();
    return 1;
  }
}

void getbuf_cookie() {
  getbuf_while(isblank, ignore_char);
  
  getbuf_while(istchar, putchar_check);

  getbuf_while(isblank, ignore_char);

  if (getbuf_char() != '=')
    fatal("unexpected response");
  putchar('=');

  getbuf_while(isblank, ignore_char);

  char c = getbuf_char();
  int isqstr = (c == '"');
  if (!isqstr) {
    if (!istchar(c))
      fatal("unexpected response");
    putchar(c);
    getbuf_while(istchar, putchar_check);
  } else {
    putchar('"');
    for (;;) {
      getbuf_while(isqdtext, putchar_check);
      if ((c = getbuf_char()) == '"') {
	  putchar('"');
	  break;
      }
      int d = getbuf_char();
      if (isquotedpair(c, d))
	putchar(d);
      else
        fatal("unexpected response");
    }
  }
  putchar('\n');

  getbuf_ignore_line();
}

void getbuf_transfer_encoding() {
  static const char *strchunked = "chunked";
  static const size_t len = 7;
  
  for (;;) {
    getbuf_while(isblank, ignore_char);
  
    size_t i;
    for (i = 0; i < len; ++i) {
      int c = getbuf_char();
      if (Blen == 0)
	fatal("unexpected response");
      if (tolower(c) != strchunked[i]) {
	ungetbuf_char();
	break;
      }
    }

    ChunkedIsLast = (i == len);
    getbuf_while(isnot_coma_cr, ignore_char);
    
    int c = getbuf_char();

    if (Blen == 0)
      fatal("unexpected response");
    if (c == '\r') {
      if (getbuf_char() != '\n')
	fatal("unexpected response");
      else
	return;
    }
  }
}

size_t getbuf_header(const char *fields[], size_t fsize) {
  int c = getbuf_char();
  if (Blen == 0)
    fatal("unexpected response");

  if (c == ':')
    fatal("unexpected response");
  
  if (c == '\r') {
    if (getbuf_char() == '\n')
      return fsize + 1; // no more headers
    else
      fatal("unexpected response");
  }
  
  size_t n = 0;
  for (size_t i = 0; i < fsize; ++i) {
    size_t m = strlen(fields[i]);
    if (n < m)
      n = m;
  }

  char *op = malloc(n + 1);
  size_t l;
  for (l = 0; l < n && c != ':'; ++l) {
    op[l] = tolower(c);
    c = getbuf_char();
    if (Blen == 0)
      fatal("unexpected response");
  }
  op[l] = '\0';
  
  if (c == ':') {
    size_t res;
    for (res = 0; res < fsize; ++res) {
      if (strcmp(op, fields[res]) == 0)
        break;
    }
    free(op);
    return res;
  } else {
    free(op);
    getbuf_ignore_until(':');
    return fsize;
  }
}

void getbuf_headers() {
  static const char *fields[] = {"set-cookie", "transfer-encoding"}; // must be sorted
  static const size_t fsize = 2;
  
  size_t i;
  while ((i = getbuf_header(fields, fsize)) <= fsize) {
    switch (i) {
    case 0:
      getbuf_cookie();
      break;
    case 1:
      getbuf_transfer_encoding();
      break;
    default:
      getbuf_ignore_line();
    }
  }
}

size_t getbuf_body_normal() {
  size_t total_size = 0;
  for (;;) {
    getbuf();
    if (Blen == 0)
      return total_size;
    size_t r = Blen - Bi;
    if (total_size > SIZE_MAX - r)
      fatal("Message body too big");
    total_size += r;
    Bi = Blen;
  }
}

size_t getbuf_chunk_size() {
  size_t chunk_size = 0;
  int c = getbuf_char();
  if (Blen == 0)
    fatal("unexpected response");
  while (isxdigit(c)) {
    if (chunk_size > (SIZE_MAX / 16))
      fatal("Chunk size overflow");
    chunk_size *= 16;
    size_t d = hex_char(c);
    if (chunk_size > SIZE_MAX - d)
      fatal("Chunk size overflow");
    chunk_size += d;
    c = getbuf_char();
    if (Blen == 0)
      fatal("unexpected response");
  }
  ungetbuf_char();
  getbuf_ignore_line();
  return chunk_size;
}

size_t getbuf_body_chunked() {
  size_t total_size = 0;
  size_t chunk_size = getbuf_chunk_size();
  while (chunk_size > 0) {
    if (total_size > SIZE_MAX - chunk_size)
      fatal("Body size overflow");
    total_size += chunk_size;
    getbuf_ignore(chunk_size);
    getbuf_crlf();
    chunk_size = getbuf_chunk_size();
  }
  return total_size;
}

void getbuf_body() {
  size_t total_size;
  if (ChunkedIsLast)
    total_size = getbuf_body_chunked();
  else
    total_size = getbuf_body_normal();
  printf("Dlugosc zasobu: %zu\n", total_size);
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

  size_t argv1len = strlen(argv[1]);
  size_t d = findchar(argv[1], argv1len, ':');
  if (d + 1 >= argv1len)
    fatal("missing port number");
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

  putbuf_request(argv[3]);
  putbuf_host(argv[3]);
  putbuf("Connection:close\r\n", 18);
  putbuf_cookies(argv[2]);

  //Buffer[Bi] = '\0';
  //fprintf(stderr, "%s", Buffer);
  putbuf_end();
  Bi = 0;
  Blen = 0;
  
  if (getbuf_response_line_ok()) {
    getbuf_headers();
    getbuf_body();
  }
  
  if (close(Sock) < 0)
    syserr("close");
  
  return 0;
}
