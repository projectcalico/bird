/*
 *	BIRD -- Core Configuration
 *
 *	(c) 1998 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

CF_HDR

CF_DECLS

CF_KEYWORDS(ROUTER, ID)

%type <i> idval

CF_GRAMMAR

CF_ADDTO(conf, rtrid)
rtrid: ROUTER ID idval {
   router_id = $3;
 }
 ;

idval:
   NUM
 | IPA { $$ = ipa_to_u32($1); }
 ;

CF_CODE

CF_END
