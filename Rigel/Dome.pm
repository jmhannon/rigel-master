package Dome;

#  If it receives none in eight minutes, DDW will interpret that as a
#  communication failure, and will rotate the dome to HOME,
#  close the shutter, and turn off the SLAVE function.
#
#  DDW will shut the dome after 8 minutes, AND will generate a 2 second signal
#  that can be used to operate a relay to reboot the computer

use common::sense;
use feature 'signatures';
use AnyEvent;
use AnyEvent::SerialPort;
use Text::CSV_XS;
use Data::Dumper;

sub new
{
	my $class  = shift;
	my $self = {
		port => '',
		status => 'not connected',
		connected => 0,
		connect_event => sub() {  },
		handle => 0,
		@_,
	};
	bless($self, $class);
	return $self;
}

sub ginf($self)
{
	$self->{handle}->push_write('GINF');
}


# if no push_read events are on the stack,
# then this reader will be called to handle the io.
# we will just add it to the lines array
sub reader($self, $handle)
{
	state $buf = '';
	state $csv = Text::CSV_XS->new ({ binary => 1, auto_diag => 1 });

	$buf .= $handle->{rbuf};
	$handle->{rbuf} = '';
	exit if (! $buf);
	print("Start [$buf]\n");

	# T, Pnnnnn  (0-32767) means its moving
	# V.... \n\n is an Info Packet
	# S, Pnnnn means shutter open/close

	# dump stuff until we get to the start of something we recognize
	my $again = 1;
	while ($again)
	{
		given (substr($buf, 0, 1))
		{
			when ('T')
			{
				$self->{status} = 'turning...';
				substr($buf, 0, 1, '');
			}
			when ('S')
			{
				$self->{status} = 'shutter...';
				substr($buf, 0, 1, '');
			}
			when ('P')
			{
				if ($buf =~ /^P(\d{4})/)
				{
					$self->{status} = "Azm $1";
					substr($buf, 0, 5, '');
				}
				else
				{
					$again = 0;
					# if we receive Pjnk1234 we'll collect till out of mem
					if (length($buf) > 15)
					{
						#bah..  buf is weird, kill it
						$buf = '';
					}
				}
			}
			when ('V')
			{
				my $at = CORE::index($buf, "\r\r");
				if ($at > -1)
				{
					my $status = $csv->parse(substr($buf, 0, $at));
					my @columns = $csv->fields();
					$self->{status} = Dumper(\@columns);
					print "STATUS: $self->{status}\n";
					$buf = substr($buf, $at+2);
					if ($self->{connect_event})
					{
						$self->{connect_event}->();
						$self->{connect_event} = 0;
					}
				}
				else
				{
					$again = 0;
					if (length($buf) > 200)
					{
						#something very wrong with this status, kill it
						$buf = '';
					}
				}
			}
			default
			{
				#toss it
				substr($buf, 0, 1, '');
			}
		}
		print("Status [$self->{status}]\n");
		$again = 0 if (length($buf) == 0);
	}
	print("End [$buf]\n");
}

sub disconnect($self)
{
	$self->{port} => 0;
	$self->{connected} = 0;
	$self->{handle} = 0;
	$self->{status} = 'not connected';
}

sub connect($self, $port)
{
	$self->{port} => $port;
	$self->{connected} = 0;
	$self->{handle} = 0;
	return unless $port;

	$self->{status} = 'connecting...';
	eval {
		$self->{handle} = AnyEvent::SerialPort->new(
			serial_port => $self->{port},   #defaults to 9600, 8n1
			on_read		=> sub {
				$self->reader(@_)
			},
			on_error	=> sub($hdl, $fatal, $msg) {
				$self->{status} = "dome serial port error: $msg";
				$hdl->destroy;
				$self->{connected} = 0;
			},
			on_eof		=> sub {
				$self->{connected} = 0;
			},
		);
	};
	if ($@) {
		print "dome connect failed: $@\n";
		$self->{status} = "connect failed: $@";
		$self->{handle} = 0;
	}
	else
	{
		# get a status update.
		$self->{handle}->push_write('GINF');
		$self->{connected} = 1;
		$self->{status} = "reading...";
	}

}


1;
