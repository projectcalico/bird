/*
 *	Configuration for Linux 2.1/2.2 based systems without Netlink
 *
 *	(c) 1998--1999 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#define CONFIG_AUTO_ROUTES
#define CONFIG_ALL_MULTICAST
#undef CONFIG_SELF_CONSCIOUS

#define CONFIG_UNIX_IFACE
#define CONFIG_UNIX_SET
#define CONFIG_LINUX_SCAN

/*
Link: sysdep/linux
Link: sysdep/unix
 */
