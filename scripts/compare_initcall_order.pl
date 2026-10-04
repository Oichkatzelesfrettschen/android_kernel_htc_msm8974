#!/usr/bin/env perl
# SPDX-License-Identifier: GPL-2.0
#
# Compare the initcall order of two kernels: a control System.map from a
# build without LTO, and either the System.map of an LTO build or the
# .tmp_initcalls.lds that scripts/generate_initcall_order.pl wrote for it.
# Each initcall reduces to its level and function name, so the per-site
# LTO symbol names compare equal to the plain ones. Both sequences are
# written to OUTPUT_PREFIX.{control,lto}.tsv; the exit status is nonzero at
# the first difference.
#
# Usage: compare_initcall_order.pl CONTROL_MAP LTO_MAP_OR_LDS OUTPUT_PREFIX

use strict;
use warnings;

@ARGV == 3 or die "usage: $0 CONTROL_MAP LTO_MAP_OR_LDS OUTPUT_PREFIX\n";
my ($control_path, $lto_path, $output_prefix) = @ARGV;

my $lto_name = qr/^__initcall__kmod_[A-Za-z0-9_]+__\d+_\d+_(.+)$/;

# Strip the level suffix that __define_initcall() appends to the function.
sub function_of {
	my ($path, $name, $level, $tail, $per_site) = @_;
	my $function = $tail;
	if ($level eq 'con' || $level eq 'sec') {
		# console_initcall() and security_initcall() append no suffix
		# to the plain name and "con" or "sec" to the per-site name.
		if ($per_site) {
			$function =~ s/\Q$level\E$//
				or die "$path: $name lacks level $level\n";
		}
	} else {
		$function =~ s/\Q$level\E$//
			or die "$path: $name lacks level $level\n";
	}
	length($function) or die "$path: empty function in $name\n";
	return $function;
}

sub read_map {
	my ($path) = @_;
	open my $input, '<', $path or die "cannot read $path: $!\n";
	my (%boundaries, @symbols);
	while (my $line = <$input>) {
		my ($address, $name) = $line =~ /^([0-9a-fA-F]+)\s+\S\s+(\S+)$/;
		next unless defined $name;
		if ($name =~ /^__(?:initcall(?:[0-7]|rootfs)?|con_initcall|security_initcall)_(?:start|end)$/) {
			$boundaries{$name} = hex($address);
		} elsif ($name =~ /^__initcall_/) {
			push @symbols, [hex($address), $name];
		}
	}
	close $input or die "cannot close $path: $!\n";
	for my $required (qw(__initcall_start __initcall0_start __initcall1_start
		__initcall2_start __initcall3_start __initcall4_start
		__initcall5_start __initcallrootfs_start __initcall6_start
		__initcall7_start __initcall_end __con_initcall_start
		__con_initcall_end __security_initcall_start
		__security_initcall_end)) {
		exists $boundaries{$required} or die "$path: missing $required\n";
	}
	my @ranges = (
		['early', '__initcall_start', '__initcall0_start'],
		(map { [$_, "__initcall${_}_start",
			"__initcall" . ($_ + 1) . "_start"] } 0 .. 4),
		['5', '__initcall5_start', '__initcallrootfs_start'],
		['rootfs', '__initcallrootfs_start', '__initcall6_start'],
		['6', '__initcall6_start', '__initcall7_start'],
		['7', '__initcall7_start', '__initcall_end'],
		['con', '__con_initcall_start', '__con_initcall_end'],
		['sec', '__security_initcall_start', '__security_initcall_end'],
	);
	my @rows;
	for my $symbol (sort { $a->[0] <=> $b->[0] } @symbols) {
		my ($address, $name) = @$symbol;
		my @matching = grep {
			$address >= $boundaries{$_->[1]} &&
			$address < $boundaries{$_->[2]}
		} @ranges;
		@matching == 1 or die "$path: $name at $address belongs to " .
			scalar(@matching) . " ranges\n";
		my $level = $matching[0][0];
		my ($tail, $per_site);
		if ($name =~ $lto_name) {
			($tail, $per_site) = ($1, 1);
		} else {
			($tail = $name) =~ s/^__initcall_//;
			$per_site = 0;
		}
		# A *_sync initcall sits in its level's range with an "s" suffix.
		$level .= 's' if $level =~ /^[0-7]$/ && $tail =~ /\Q$level\Es$/;
		push @rows, [$level, function_of($path, $name, $level, $tail,
			$per_site), $name];
	}
	@rows or die "$path: no initcalls\n";
	return \@rows;
}

sub read_order_script {
	my ($path) = @_;
	open my $input, '<', $path or die "cannot read $path: $!\n";
	my @rows;
	while (my $line = <$input>) {
		next unless $line =~ /KEEP/;
		my ($section, $name) = $line =~
			/^\s*KEEP\(\*\(\.(initcall(?:early|rootfs|[0-7]s?)|con_initcall|security_initcall)\.init\.\.([^()]+)\)\)\s*$/;
		defined $name or die "$path: malformed initcall order line $line";
		my $level = $section eq 'con_initcall' ? 'con' :
			$section eq 'security_initcall' ? 'sec' :
			($section =~ /^initcall(.+)$/)[0];
		my ($tail) = $name =~ $lto_name;
		defined $tail or die "$path: malformed symbol $name\n";
		push @rows, [$level, function_of($path, $name, $level, $tail, 1),
			$name];
	}
	close $input or die "cannot close $path: $!\n";
	@rows or die "$path: empty initcall order\n";
	return \@rows;
}

my $control = read_map($control_path);
my $lto = $lto_path =~ /\.lds$/ ? read_order_script($lto_path) :
	read_map($lto_path);
for my $side (['control', $control], ['lto', $lto]) {
	my ($name, $rows) = @$side;
	open my $output, '>', "$output_prefix.$name.tsv"
		or die "cannot write $output_prefix.$name.tsv: $!\n";
	for my $index (0 .. $#$rows) {
		print {$output} join("\t", $index, @{ $rows->[$index] }), "\n";
	}
	close $output or die "cannot close $output_prefix.$name.tsv: $!\n";
}
@$control == @$lto or die "initcall counts differ: " . scalar(@$control) .
	" control, " . scalar(@$lto) . " LTO\n";
for my $index (0 .. $#$control) {
	my $expected = join("\t", @{ $control->[$index] }[0, 1]);
	my $actual = join("\t", @{ $lto->[$index] }[0, 1]);
	$expected eq $actual
		or die "initcall order differs at $index: $expected vs $actual\n";
}
print "matched " . scalar(@$control) . " initcalls\n";
