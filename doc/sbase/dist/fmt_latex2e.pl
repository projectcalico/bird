#
#  fmt_latex2e.pl
#
#  $Id$
#
#  LaTeX-specific driver stuff
#
#  © Copyright 1996, Cees de Groot
#
package SGMLTools::fmt_latex2e;
use strict;

use SGMLTools::CharEnts;
use SGMLTools::Vars;
use SGMLTools::Lang;

use File::Copy;

my $latex2e = {};
$latex2e->{NAME} = "latex2e";
$latex2e->{HELP} = <<EOF;
  Note that this output format requires LaTeX 2e.

EOF
$latex2e->{OPTIONS} = [
   { option => "output", type => "l", 
     'values' => [ "dvi", "tex", "ps" ], short => "o" },
   { option => "bibtex", type => "f",  short => "b" },
   { option => "makeindex", type => "f",  short => "m" },
   { option => "pagenumber", type => "i", short => "n" },
   { option => "quick",  type => "f",  short => "q" }
];
$latex2e->{output} = "dvi";
$latex2e->{pagenumber} = 1;
$latex2e->{quick}  = 0;
$latex2e->{bibtex}  = 0;
$latex2e->{makeindex} = 0;
$latex2e->{preNSGMLS} = sub {
  $global->{NsgmlsOpts} .= " -ifmttex ";
};

$Formats{$latex2e->{NAME}} = $latex2e;


# extra `\\' here for standard `nsgmls' output
my %latex2e_escapes;
$latex2e_escapes{'#'} = '\\\\#';
$latex2e_escapes{'$'} = '\\\\$';
$latex2e_escapes{'%'} = '\\\\%';
$latex2e_escapes{'&'} = '\\\\&';
$latex2e_escapes{'~'} = '\\\\~{}';
$latex2e_escapes{'_'} = '\\\\_';
$latex2e_escapes{'^'} = '\\\\^{}';
$latex2e_escapes{'\\'} = '\\verb+\\+';
$latex2e_escapes{'{'} = '\\\\{';
$latex2e_escapes{'}'} = '\\\\}';
$latex2e_escapes{'>'} = '{$>$}';
$latex2e_escapes{'<'} = '{$<$}';	# wouldn't happen, but that's what'd be
$latex2e_escapes{'|'} = '{$|$}';

my $in_verb;

# passed to `parse_data' below in latex2e_preASP
my $latex2e_escape = sub {
    my ($data) = @_;

    if (!$in_verb) {
	# escape special characters
	$data =~ s|([#\$%&~_^\\{}<>\|])|$latex2e_escapes{$1}|ge;
    }

    return ($data);
};

#
#  Translate character entities and escape LaTeX special chars.
#
$latex2e->{preASP} = sub
{
  my ($infile, $outfile) = @_;

  # note the conversion of `sdata_dirs' list to an anonymous array to
  # make a single argument
  my $tex_char_maps = load_char_maps ('.2tex', [ Text::EntityMap::sdata_dirs() ]);

  # ASCII char maps are used in the verbatim environment because TeX
  # ignores all the escapes
  my $ascii_char_maps = load_char_maps ('.2ab', [ Text::EntityMap::sdata_dirs() ]);
  $ascii_char_maps = load_char_maps ('.2l1b', [ Text::EntityMap::sdata_dirs() ]) if $global->{charset} eq "latin";

  my $char_maps = $tex_char_maps;

  # used in `latex2e_escape' anonymous sub to switch between escaping
  # characters from SGML source or not, depending on whether we're in
  # a VERB or CODE environment or not
  $in_verb = 0;

  while (<$infile>)
    {
      if (/^-/)
        {
	    my ($str) = $';
	    chop ($str);
	    print $outfile "-" . parse_data ($str, $char_maps, $latex2e_escape) . "\n";
        }
      elsif (/^A/)
        {
	  /^A(\S+) (IMPLIED|CDATA|NOTATION|ENTITY|TOKEN)( (.*))?$/
	      || die "bad attribute data: $_\n";
	  my ($name,$type,$value) = ($1,$2,$4);
	  if ($type eq "CDATA")
	    {
	      # CDATA attributes get translated also
	      if ($name eq "URL" or $name eq "ID")
	        {
		  # URL for url.sty is a kind of verbatim...
		  my $old_verb = $in_verb;
		  $in_verb = 1;
		  $value = parse_data ($value, $ascii_char_maps, 
		      $latex2e_escape);
		  $in_verb = $old_verb;
		}
	      else
	        {
	          $value = parse_data ($value, $char_maps, $latex2e_escape);
		}
	    }
	  print $outfile "A$name $type $value\n";
        }
      elsif (/^\((VERB|CODE)/)
        {
	  print $outfile $_;
	  # going into VERB/CODE section
	  $in_verb = 1;
	  $char_maps = $ascii_char_maps;
	}
      elsif (/^\)(VERB|CODE)/)
        {
	  print $outfile $_;
	  # leaving VERB/CODE section
	  $in_verb = 0;
	  $char_maps = $tex_char_maps;
	}
      else
        {
	  print $outfile $_;
        }
    }
};

#
#  Take the sgmlsasp output, and make something
#  useful from it.
#
$latex2e->{postASP} = sub
{
  my $infile = shift;
  my $filename = $global->{filename};
  $ENV{TEXINPUTS} .= ":$main::LibDir";

  #
  #  Set the correct \documentclass options. The if statement is just
  #  a small optimization.
  #
  if ($global->{language} ne "en" ||
      $global->{papersize} ne "a4" ||
      $latex2e->{pagenumber} != 1 ||
      $global->{pass} ne "" ||
      $latex2e->{makeindex})
    {
      my $langlit = ISO2English ($global->{language});
      $langlit = ($langlit eq 'english') ? "" : ",$langlit"; 
      my $replace = $global->{papersize} . 'paper' .   $langlit;
      open OUTFILE, ">$filename.tex";
      while (<$infile>)
        {
          if (/^\\documentclass/) 
	    {
	      s/\\documentclass\[.*\]/\\documentclass\[$replace\]/;
              $_ = $_ . "\\makeindex\n" if ($latex2e->{makeindex});
            }  
          if (/%end-preamble/)
	    {
	      if ($latex2e->{pagenumber}) 
                {
		  $_ = $_ . '\setcounter{page}{'. 
		       $latex2e->{pagenumber} . "}\n";
	        } 
	      else 
	        {
		  $_ = $_ . "\\pagestyle{empty}\n";
	        }
	      $_ = $_ . $global->{pass} . "\n" if ($global->{pass});
	    }
	  print OUTFILE;
	}
      close OUTFILE;
    }
  else
    {
      copy ($infile, "$filename.tex");
    }

  #
  #  LaTeX, dvips, and assorted cleanups.
  #
  if ($latex2e->{output} eq "tex")
    { 
      return 0; 
    }

  #
  # Run LaTeX in nonstop mode so it won't prompt & hang on errors.
  # Suppress the output of LaTeX on all but the last pass, after
  # references have been resolved.  This avoids large numbers of
  # spurious warnings.
  #
  my ($latexcommand) = "latex '\\nonstopmode\\input{$filename.tex}'";
  my ($suppress) = $latex2e->{quick} ? "" : ' >/dev/null';

  system $latexcommand . $suppress  || die "LaTeX problem\n";
  $latex2e->{bibtex} && system "bibtex $filename.tex";
  $latex2e->{quick} || system $latexcommand . ' >/dev/null';
  $latex2e->{quick} || system $latexcommand;
  if ($global->{debug} == 0)
    {
      my @suffixes = qw(log blg aux toc lof lot dlog bbl);
      for my $suf (@suffixes)
        {
          unlink "$filename.$suf";
        }
    }
  if ($latex2e->{output} eq "dvi")
    {
      $global->{debug} || unlink "$filename.tex";
      return 0;
    }
  `dvips -q -t $global->{papersize} -o $filename.ps $filename.dvi`;
  $global->{debug} || unlink ("$filename.dvi", "$filename.tex");

  return 0;
};

1;
