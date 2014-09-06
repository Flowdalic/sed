/* Copyright (C) 2001, 2003, 2014 Free Software Foundation, Inc.
   This file is part of the GNU C Library.

   The GNU C Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2.1 of the License, or (at your option) any later version.

   The GNU C Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with the GNU C Library; if not, write to the Free
   Software Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
   02110-1301 USA.  */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <alloca.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <iconv.h>
#include <locale.h>
#ifdef HAVE_MCHECK_H
#include <mcheck.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <regex.h>


static iconv_t cd;
static char *mem;
static char *umem;
static size_t memlen;
static size_t umemlen;
static int test_expr (const char *expr, int expected, int expectedicase);
static int run_test (const char *expr, const char *mem, size_t memlen,
		     int icase, int expected);
static int run_test_backwards (const char *expr, const char *mem,
			       size_t memlen, int icase, int expected);


int
main (int argc, char *argv[])
{
  int fd;
  struct stat st;
  int result;
  char *inmem;
  char *outmem;
  size_t inlen;
  size_t outlen;

#ifdef HAVE_MCHECK_H
  mtrace ();
#endif

  if (!argv[1])
    exit (1);

  /* Make the content of the file available in memory.  */
  fd = open (argv[1], O_RDONLY);
  if (fd == -1)
    error (EXIT_FAILURE, errno, "cannot open %s", basename (argv[1]));

  if (fstat (fd, &st) != 0)
    error (EXIT_FAILURE, errno, "cannot stat %s", basename (argv[1]));
  memlen = st.st_size;

  mem = (char *) malloc (memlen + 1);
  if (mem == NULL)
    error (EXIT_FAILURE, errno, "while allocating buffer");

  if ((size_t) read (fd, mem, memlen) != memlen)
    error (EXIT_FAILURE, 0, "cannot read entire file");
  mem[memlen] = '\0';

  close (fd);

  /* We have to convert a few things from Latin-1 to UTF-8.  */
  cd = iconv_open ("UTF-8", "ISO-8859-1");
  if (cd == (iconv_t) -1)
    error (EXIT_FAILURE, errno, "cannot get conversion descriptor");

  /* For the second test we have to convert the file content to UTF-8.
     Since the text is mostly ASCII it should be enough to allocate
     twice as much memory for the UTF-8 text than for the Latin-1
     text.  */
  umem = (char *) calloc (2, memlen);
  if (umem == NULL)
    error (EXIT_FAILURE, errno, "while allocating buffer");

  inmem = mem;
  inlen = memlen;
  outmem = umem;
  outlen = 2 * memlen - 1;
  iconv (cd, &inmem, &inlen, &outmem, &outlen);
  umemlen = outmem - umem;
  if (inlen != 0)
    error (EXIT_FAILURE, errno, "cannot convert buffer");

#ifdef DEBUG
  re_set_syntax (RE_DEBUG);
#endif

  /* Run the actual tests.  All tests are run in a single-byte and a
     multi-byte locale.  */
  result = test_expr ("[�������������������]", 2, 2);
  result |= test_expr ("G.ran", 2, 3);
  result |= test_expr ("G.\\{1\\}ran", 2, 3);
  result |= test_expr ("G.*ran", 3, 44);
  result |= test_expr ("[����]", 0, 0);
  result |= test_expr ("Uddeborg", 2, 2);
  result |= test_expr (".Uddeborg", 2, 2);

  /* Free the resources.  */
  free (umem);
  iconv_close (cd);
  free (mem);

  return result;
}


static int
test_expr (const char *expr, int expected, int expectedicase)
{
  int result;
  printf ("\nTest \"%s\" with 8-bit locale\n", expr);
  result = run_test (expr, mem, memlen, 0, expected);
  printf ("\nTest \"%s\" with 8-bit locale, case insensitive\n", expr);
  result |= run_test (expr, mem, memlen, 1, expectedicase);
  printf ("\nTest \"%s\" backwards with 8-bit locale\n", expr);
  result |= run_test_backwards (expr, mem, memlen, 0, expected);
  printf ("\nTest \"%s\" backwards with 8-bit locale, case insensitive\n",
	  expr);
  result |= run_test_backwards (expr, mem, memlen, 1, expectedicase);
  return result;
}


static int
run_test (const char *expr, const char *mem, size_t memlen, int icase,
	  int expected)
{
  regex_t re;
  int err;
  size_t offset;
  int cnt;

  err = regcomp (&re, expr, REG_NEWLINE | (icase ? REG_ICASE : 0));
  if (err != REG_NOERROR)
    {
      char buf[200];
      regerror (err, &re, buf, sizeof buf);
      error (EXIT_FAILURE, 0, "cannot compile expression: %s", buf);
    }

  cnt = 0;
  offset = 0;
  assert (mem[memlen] == '\0');
  while (offset < memlen)
    {
      regmatch_t ma[1];
      const char *sp;
      const char *ep;

      err = regexec (&re, mem + offset, 1, ma, 0);
      if (err == REG_NOMATCH)
	break;

      if (err != REG_NOERROR)
	{
	  char buf[200];
	  regerror (err, &re, buf, sizeof buf);
	  error (EXIT_FAILURE, 0, "cannot use expression: %s", buf);
	}

      assert (ma[0].rm_so >= 0);
      sp = mem + offset + ma[0].rm_so;
      while (sp > mem && sp[-1] != '\n')
	--sp;

      ep = mem + offset + ma[0].rm_so;
      while (*ep != '\0' && *ep != '\n')
	++ep;

      printf ("match %d: \"%.*s\"\n", ++cnt, (int) (ep - sp), sp);

      offset = ep + 1 - mem;
    }

  regfree (&re);

  /* Return an error if the number of matches found is not match we
     expect.  */
  return cnt != expected;
}


static int
run_test_backwards (const char *expr, const char *mem, size_t memlen,
		    int icase, int expected)
{
  regex_t re;
  const char *err;
  size_t offset;
  int cnt;

  re_set_syntax ((RE_SYNTAX_POSIX_BASIC & ~RE_DOT_NEWLINE)
		 | RE_HAT_LISTS_NOT_NEWLINE
		 | (icase ? RE_ICASE : 0));

  memset (&re, 0, sizeof (re));
  re.fastmap = malloc (256);
  if (re.fastmap == NULL)
    error (EXIT_FAILURE, errno, "cannot allocate fastmap");

  err = re_compile_pattern (expr, strlen (expr), &re);
  if (err != NULL)
    error (EXIT_FAILURE, 0, "cannot compile expression: %s", err);

  if (re_compile_fastmap (&re))
    error (EXIT_FAILURE, 0, "couldn't compile fastmap");

  cnt = 0;
  offset = memlen;
  assert (mem[memlen] == '\0');
  while (offset <= memlen)
    {
      int start;
      const char *sp;
      const char *ep;

      start = re_search (&re, mem, memlen, offset, -offset, NULL);
      if (start == -1)
	break;

      if (start == -2)
	error (EXIT_FAILURE, 0, "internal error in re_search");

      sp = mem + start;
      while (sp > mem && sp[-1] != '\n')
	--sp;

      ep = mem + start;
      while (*ep != '\0' && *ep != '\n')
	++ep;

      printf ("match %d: \"%.*s\"\n", ++cnt, (int) (ep - sp), sp);

      offset = sp - 1 - mem;
    }

  regfree (&re);

  /* Return an error if the number of matches found is not match we
     expect.  */
  return cnt != expected;
}
