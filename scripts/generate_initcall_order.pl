#!/usr/bin/env perl
# SPDX-License-Identifier: GPL-2.0
#
# Write the linker script that merges the per-initcall sections of an LTO
# link (include/linux/init.h, CONFIG_LTO_CLANG) back into the level sections
# in vmlinux link order: input files in argument order, archive members in
# archive order, and initcalls of one object in __COUNTER__ order.
#
# Usage: NM=llvm-nm generate_initcall_order.pl OBJECT_OR_ARCHIVE... > lds

use strict;
use warnings;

my $nm = $ENV{NM} or die "NM must name the LLVM symbol reader\n";
@ARGV or die "at least one ordered vmlinux input is required\n";

my @levels = qw(early 0 0s 1 1s 2 2s 3 3s 4 4s 5 5s rootfs 6 6s 7 7s con sec);
my %valid_level = map { $_ => 1 } @levels;
my %sections;
my %seen_symbols;
my $ordinary_count = 0;

sub output_section {
	my ($level) = @_;
	return $level eq 'con' ? '.con_initcall.init' :
		$level eq 'sec' ? '.security_initcall.init' :
		".initcall${level}.init";
}

sub append_member {
	my ($file, $member, $calls) = @_;
	my %seen_counters;
	for my $call (sort { $a->{counter} <=> $b->{counter} } @$calls) {
		my ($symbol, $level, $counter) =
			@{$call}{qw(symbol level counter)};
		die "duplicate initcall counter $counter in $file:$member\n"
			if $seen_counters{$counter}++;
		die "duplicate initcall symbol $symbol\n"
			if $seen_symbols{$symbol}++;
		push @{ $sections{$level} },
			output_section($level) . "..$symbol";
		$ordinary_count++ unless $level eq 'con' || $level eq 'sec';
	}
}

for my $file (@ARGV) {
	-f $file or die "missing initcall input $file\n";
	open my $input, '-|', $nm, '--defined-only', '--quiet', $file
		or die "cannot execute $nm for $file: $!\n";
	my $member = $file;
	my @calls;
	while (my $line = <$input>) {
		chomp $line;
		# llvm-nm names each archive member on a line of its own.
		if ($line =~ /^([^\s].*):$/) {
			append_member($file, $member, \@calls);
			@calls = ();
			$member = $1;
			next;
		}
		my ($symbol) = $line =~ /^\s*\S+\s+[A-Za-z]\s+(\S+)\s*$/;
		next unless defined $symbol && $symbol =~ /^__initcall_/;
		my ($counter, $tail) = $symbol =~
			/^__initcall__kmod_[A-Za-z0-9_]+__(\d+)_\d+_(.+)$/;
		die "malformed initcall symbol $symbol in $file:$member\n"
			unless defined $tail;
		my ($function, $level) =
			$tail =~ /^(.+?)(early|rootfs|[0-7]s?|con|sec)$/;
		die "unknown initcall level in $symbol in $file:$member\n"
			unless defined $function && $valid_level{$level};
		push @calls, { symbol => $symbol, level => $level,
			counter => int($counter) };
	}
	close $input or die "$nm failed for $file (status $?)\n";
	append_member($file, $member, \@calls);
}

die "no ordinary initcalls found\n" unless $ordinary_count;

print "SECTIONS {\n";
for my $level (@levels) {
	next unless $sections{$level};
	print "  ", output_section($level), " : {\n";
	print "    KEEP(*($_))\n" for @{ $sections{$level} };
	print "  }\n";
}
print "}\n";
