m4_divert(-1)m4_dnl
#
#	BIRD -- Generator of Configuration Keyword List
#
#	(c) 1998 Martin Mares <mj@atrey.karlin.mff.cuni.cz>
#
#	Can be freely distributed and used under the terms of the GNU GPL.
#

# Simple iterator
m4_define(CF_itera, `m4_ifelse($#, 1, [[CF_iter($1)]], [[CF_iter($1)[[]]CF_itera(m4_shift($@))]])')
m4_define(CF_iterate, `m4_define([[CF_iter]], m4_defn([[$1]]))CF_itera($2)')

# Keywords are translated to C initializers
m4_define(CF_handle_kw, `m4_divert(0){ "m4_translit($1,[[A-Z]],[[a-z]])", $1 },
m4_divert(-1)')
m4_define(CF_keywd, `m4_ifdef([[CF_tok_$1]],,[[m4_define([[CF_tok_$1]],1)CF_handle_kw($1)]])')
m4_define(CF_KEYWORDS, `m4_define([[CF_toks]],[[]])CF_iterate([[CF_keywd]], [[$@]])m4_ifelse(CF_toks,,,%token[[]]CF_toks
)DNL')

# As we are processing C source, we must access all M4 primitives via
# m4_* and also set different quoting convention: `[[' and ']]'
m4_changequote([[,]])
