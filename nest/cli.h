/*
 *	BIRD Internet Routing Daemon -- Command-Line Interface
 *
 *	(c) 1999 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#ifndef _BIRD_CLI_H_
#define _BIRD_CLI_H_

#include "lib/resource.h"
#include "lib/event.h"

#define CLI_RX_BUF_SIZE 4096
#define CLI_TX_BUF_SIZE 4096

struct cli_out {
  struct cli_out *next;
  byte *wpos, *outpos, *end;
  byte buf[0];
};

typedef struct cli {
  pool *pool;
  void *priv;				/* Private to sysdep layer */
  int inited;
  byte rx_buf[CLI_RX_BUF_SIZE];
  byte *rx_pos, *rx_aux;		/* sysdep */
  struct cli_out *tx_buf, *tx_pos, *tx_write;
  event *event;
} cli;

extern pool *cli_pool;

cli *cli_new(void *);
void cli_init(void);
void cli_free(cli *);
void cli_kick(cli *);
void cli_written(cli *);
void cli_printf(cli *, int, char *, ...);

/* Function provided by sysdep layer */

int cli_write(cli *);
void cli_disconnect(cli *);
int cli_get_command(cli *);

#endif
