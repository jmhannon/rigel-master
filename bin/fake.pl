#!/usr/bin/perl
use common::sense;
use feature 'signatures';
use AnyEvent;
use AnyEvent::Handle;
use AnyEvent::Socket;
use Data::Dumper;


my $cv = AnyEvent->condvar;
my @clientList;

tcp_server(
	undef,
	7623,
	sub {
		my ($fh, $host, $port) = @_ or die "server failed: $!";
		my $dec;
		$dec = AnyEvent::Handle->new(
			fh     => $fh,
			on_error => sub {
				print("socket error: $_[2]\n");
				$_[0]->destroy;
			},
			on_eof => sub {
				print "client disconnected\n";
			},
			on_read => \&decReader
		);
		$dec->push_read( chunk => 3, sub($handle, $data) {
				my @list = unpack('ccc', $data);
				print " got: ", Dumper(\@list);
				$dec->push_write( pack('C', 42) );
				#$dec->on_read(\&decReader);
			}
		);
		push(@clientList, $dec);
	}
);



$cv->recv;

sub decReader($handle)
{
	print $handle->{rbuf};
	if (index($handle->{rbuf}, 'epos') > -1)
	{
		$handle->push_write("12456\n");
	}
	elsif (index($handle->{rbuf}, 'findhome') > -1)
	{
		$handle->push_write("1: starting...\n");
		sleep(2);
		$handle->push_write("moving...\n");
		sleep(2);
		$handle->push_write("1: thinking...\n");
		sleep(2);
		$handle->push_write("-1: error finished\n");
	}
	$handle->{rbuf} = '';
=pod
	my $at = index($handle->{rbuf}, "\n");
	return if ($at == -1);
	my $line = substr($handle->{rbuf}, 0, $at, '');
	print("read: [$line]\n");
=cut
}


