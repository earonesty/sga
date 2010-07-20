#! /usr/bin/perl
#
# splitFasta.pl READS OUTPREFIX
#
# Split the READS fasta file into two files, one for each half of a read pair
#
my $inFile = $ARGV[0];
my $out1File = $ARGV[1];
my $out2File = $ARGV[2];

die("No input file given") if($inFile eq "");
die("No output file(s) given") if($out1File eq "" || $out2File eq "");

open(OUT1, ">$out1File");
open(OUT2, ">$out2File");
open(IN, $inFile);

while(my $header = <IN>)
{
    chomp $header;
    my $record = "";
    if($header =~ /^>/)
    {
        # parse fasta, assume 1 line per sequence
        $record = substr($header, 0, -2) . "\n" . <IN>;
    }
    elsif($header =~ /^@/)
    {
        # parse fastq
        my $record = substr($header, 0, -2) . "\n";
        $record .= <IN>;
        $record .= <IN>;
        $record .= <IN>;
    }
    else
    {
        next;
    }
    
    # emit record
    if(isFirstRead($header))
    {
        print OUT1 $record;
    }
    else
    {
        print OUT2 $record;
    }
}

close(OUT1);
close(OUT2);
close(IN);

sub isFirstRead
{
    my ($header) = @_;
    return 1 if($header =~ /A|1$/);
    return 0 if($header =~ /B|2$/);
    die("Cannot parse record $header");
}
