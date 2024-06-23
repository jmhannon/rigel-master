# pretend to be an LX200 telescope mount
package Rigel::LX200;

use common::sense;
use feature 'signatures';
use AnyEvent::Handle;
use AnyEvent::SerialPort;
use Astro::PAL;
use Astro::Coords;

=head2 new
	my $lx = Rigel::LX200->new( port => '/dev/ttyUSB0', recv => sub($cmd, $handle) {} )
=cut

sub new
{
	my $class = shift;
	my $self  = {
		fh => 0,
		recv => 0,
		@_,
	};
	bless $self, $class;

	my $tty;
	eval {
		$tty = AnyEvent::SerialPort->new(
			serial_port => [$self->{port}, [ baudrate => 9600 ] ],
			on_read => sub { $self->readSerial(@_) },
			on_error => sub {
				my ($hdl, $fatal, $msg) = @_;
				print "serial error: $msg\n";
				$hdl->destroy;
			}
		);
	};
	if ($@) {
		print "lx200 connect failed\n";
		print $@;
		return undef;
	};

	$self->{tty} = $tty;

	return $self
}

sub readSerial($self, $handle)
{
	state $buf = '';

	$buf .= $handle->{rbuf};
	$handle->{rbuf} = '';
	exit if (! $buf);
	#print "Start [$buf]\n";
	my $cmd = '';

	my $again = 1;
	while ($again)
	{
		given (substr($buf, 0, 1))
		{
			when (chr(16)) {
				$handle->push_write('P');
				substr($buf, 0, 1, '');
			}
			when (':') {
				my $at = index($buf, '#');
				if ($at > -1)
				{
					$cmd = substr($buf, 0, $at+1);
					$buf = substr($buf, $at+1);
					$self->{recv}($cmd, $self->{tty});
				}
				else {
					$again = 0;
					if (length($buf) > 200) {
						#something very wrong with this command, kill it
						$buf = '';
					}
				}
			}
			default {
				#toss it
				substr($buf, 0, 1, '');
			}
		}
		$again = 0 if (length($buf) == 0);
	}
	#print "End [$buf]\n";
}


1;

