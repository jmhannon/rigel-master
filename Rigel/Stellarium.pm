# Listen for Stellarium movement commands via tcp
package Rigel::Stellarium;

use common::sense;
use feature 'signatures';
use AnyEvent::Handle;
use AnyEvent::Socket;
use Astro::PAL;
use Astro::Coords;

sub new {
	my $this  = shift;
	my $class = ref($this) || $this;
	my $self  = {
		host => '::',
		port => 10001,
		fh => 0,
		ioh => 0,
		srv => 0,
		recv => 0,
		@_,
	};
	bless $self, $class;

	$self->{srv} =
	tcp_server $self->{host}, $self->{port}, sub {
		my ($fh, $host, $port) = @_;

		unless ($fh) {
			$self->event (error => "couldn't accept client: $!");
			return;
		}

		$self->accept_connection ($fh, $host, $port);
	}, sub {
		my ($fh, $host, $port) = @_;
		$self->{real_port} = $port;
		$self->{real_host} = $host;
		return $self->{backlog};
	};
	print "stellarium client listening on on 10001\n";
	return $self
}

sub accept_connection {
	my ($self, $fh, $h, $p) = @_;

	#only connect to one thing at a time
	if ($self->{fh})
	{
		$self->{fh} = undef;
		$self->{ioh} = undef;
	}
	$self->{fh} = $fh;
	$self->{ioh} = AnyEvent::Handle->new (
		fh       => $fh,
		on_eof   => sub { $self->do_disconnect },
		on_error => sub { $self->do_disconnect ("Error: $!") },
		on_read  => sub { $self->do_read( @_ ); }
	);
}

sub do_read($self, $handle)
{
	state $buf = '';

	$buf .= $handle->{rbuf};
	$handle->{rbuf} = '';
	exit if (! $buf);
	#print "LX [$buf]\n";
	#l = 32bit, q=64bit.
	#L = unsigned, l = signed
	#4+8+4+4 = 20
	# rpi doesnt support Q, so use two L's

	my($size, $msec1, $msec2, $ra, $dec, $status) = unpack("LLLLl", $buf);
	#print "size: $size\n";
	#print "msec: $msec\n";
	#print "  ra: $ra\n";
	#print " dec: $dec\n";

	my $ra2  = $ra * (DPI / 0x80000000);
	my $dec2 = $dec * (DPI / 0x80000000);
	$buf = '';

	my $c = new Astro::Coords(
		name => "My target",
		ra   => $ra2,
		dec  => $dec2,
		type => 'j2000',
		units=> 'radians'
	);

	$self->{recv}($c);

}

sub do_disconnect($self, $error=0)
{
	print "Stellarium disconnect\n";
	if ($error) {
		print "Error: $error\n";
	}
}

1;

