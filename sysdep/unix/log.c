/*
 *	BIRD Library -- Logging Functions
 *
 *	(c) 1998--1999 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <sys/time.h>

#include "nest/bird.h"
#include "nest/cli.h"
#include "lib/string.h"

static int log_inited;
static FILE *logf = NULL;
static FILE *dbgf = NULL;

#ifdef HAVE_SYSLOG
#include <sys/syslog.h>

static int syslog_priorities[] = {
  LOG_INFO,
  LOG_DEBUG,
  LOG_INFO,
  LOG_WARNING,
  LOG_ERR,
  LOG_NOTICE,
  LOG_CRIT
};
#endif

static char *class_names[] = {
  "???",
  "DBG",
  "INFO",
  "WARN",
  "ERR",
  "AUTH",
  "FATAL"
};

static void
vlog(int class, char *msg, va_list args)
{
  char buf[1024];
  char date[32];

  if (bvsnprintf(buf, sizeof(buf)-1, msg, args) < 0)
    bsprintf(buf + sizeof(buf) - 100, " ... <too long>");

  if (logf)
    {
      time_t now = time(NULL);
      struct tm *tm = localtime(&now);

      bsprintf(date, "%02d-%02d-%04d %02d:%02d:%02d <%s> ",
	       tm->tm_mday,
	       tm->tm_mon+1,
	       tm->tm_year+1900,
	       tm->tm_hour,
	       tm->tm_min,
	       tm->tm_sec,
	       class_names[class]);
      fputs(date, logf);
      fputs(buf, logf);
      fputc('\n', logf);
      fflush(logf);
    }
#ifdef HAVE_SYSLOG
  else if (log_inited)
    syslog(syslog_priorities[class], "%s", buf);
#endif
  else
    {
      fputs("bird: ", stderr);
      fputs(buf, stderr);
      fputc('\n', stderr);
      fflush(stderr);
    }
  cli_echo(class, buf);
}

void
log(char *msg, ...)
{
  int class = 1;
  va_list args;

  va_start(args, msg);
  if (*msg >= 1 && *msg <= 8)
    class = *msg++;
  vlog(class, msg, args);
  va_end(args);
}

void
bug(char *msg, ...)
{
  va_list args;

  va_start(args, msg);
  vlog(L_BUG[0], msg, args);
  exit(1);
}

void
die(char *msg, ...)
{
  va_list args;

  va_start(args, msg);
  vlog(L_FATAL[0], msg, args);
  exit(1);
}

void
debug(char *msg, ...)
{
  va_list args;
  char buf[1024];

  va_start(args, msg);
  if (bvsnprintf(buf, sizeof(buf), msg, args) < 0)
    bsprintf(buf + sizeof(buf) - 100, " ... <too long>\n");
  if (dbgf)
    fputs(buf, dbgf);
  va_end(args);
}

void
log_init(char *f)
{
  FILE *new;

  if (!f)
    new = stderr;
  else if (!*f)
    {
      new = NULL;
#ifdef HAVE_SYSLOG
      openlog("bird", LOG_CONS | LOG_NDELAY, LOG_DAEMON);
#endif
    }
  else if (!(new = fopen(f, "a")))
    {
      log(L_ERR "Unable to open log file `%s': %m", f);
      return;
    }
  if (logf && logf != stderr)
    fclose(logf);
  logf = new;
  log_inited = 1;
}

void
log_init_debug(char *f)
{
  if (dbgf && dbgf != stderr)
    fclose(dbgf);
  if (!f)
    dbgf = stderr;
  else if (!*f)
    dbgf = NULL;
  else if (!(dbgf = fopen(f, "a")))
    log(L_ERR "Error opening debug file `%s': %m", f);
}
