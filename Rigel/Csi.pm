package Csi;

use common::sense;
use feature 'signatures';
use AnyEvent;
use AnyEvent::Socket;
use AnyEvent::Handle;
use Text::CSV_XS;

sub new
{
	my $class  = shift;
	my $self = {
		cfg => 0,
		hwaddr => 0,
		error => '',
		connected => 0,
		homed => 0,
		connect_event => sub($msg) {  },
		@_,
	};
	bless($self, $class);
	$self->connect();
	return $self;
}

sub readHomeLine($self, $line, $cb)
{
	$cb->($line);
	# the find.cmc scripts need to return a status
	# integer as the first thing on the line:
	# 0 = success
	# -1 = error
	# anything else is ignored
	my($status) = $line =~ /^(-?\d+)/;

	if ($status eq '0')
	{
		$self->{homed} = 1;
		$self->{error} = '';
		return;
	}
	if ($status eq '-1')
	{
		$self->{homed} = 0;
		$self->{error} = $line;
		return;
	}

	# if we arent done, schedule another read
	$self->{handle}->push_read(
		line => sub($handle, $line, $eol) {$self->readHomeLine($line, $cb);}
	);
}

sub home($self, $cb)
{
	$self->{homed} = 0;
	$self->{error} = '';
	$self->{handle}->push_read(
		line => sub($handle, $line, $eol) {$self->readHomeLine($line, $cb);}
	);
	$self->{handle}->push_write("findhom(-1);\n");
}

sub monitor($self, $line, $cb=undef)
{
	#mpos,mvel,epos,evel
	#1234,
	#error
	#done

	if ($line =~ /^(\d+),(\d+),(\d+),(\d+)/)
	{
		my $mpos = $1;
		my $mvel = $2;
		my $epos = $3;
		my $evel = $4;

		my $distance = sqrt( ($mpos - $self->{mtpos})**2 );
		$self->{pct} = 1-($distance / $self->{ttldist});
		$self->{status} = sprintf("pct done: %.1f", $self->{pct});
		# schedule another read
		$self->{handle}->push_read(
			line => sub($handle, $line, $eol) {$self->monitor($line, $cb);}
		);
	}
	elsif ($line =~ /^start:\s+mpos=(\d+).*?mtpos=(\d+)/)
	{
		my $mpos = $1;
		my $mtpos = $2;
		my $distance = sqrt( ($mpos - $mtpos)**2 );

		$self->{working} = 1;
		$self->{mtpos} = $mtpos;
		$self->{ttldist} = $distance;
		# schedule another read
		$self->{handle}->push_read(
			line => sub($handle, $line, $eol) {$self->monitor($line, $cb);}
		);
	}
	elsif ($line =~ /^done:\s+(\d+),(\d+),(\d+),(\d+)/)
	{
		my $mpos = $1;
		my $mvel = $2;
		my $epos = $3;
		my $evel = $4;
		$self->{working} = 0;
		$self->{status} = 'ready';
	}
	elsif ($line =~ /^error/)
	{
		$self->{working} = 0;
		$self->{status} = $line;
	}

	if ($cb) {
		$cb->();
	}
}

sub etpos_offset($self, $offset, $cb=undef)
{
	if ($offset > 0) {
		$self->{handle}->push_write("etpos=epos+$offset;\n");
	}
	else {
		$self->{handle}->push_write("etpos=epos$offset;\n");
	}

	$self->{handle}->push_read(
		line => sub($handle, $line, $eol) {$self->monitor($line, $cb);}
	);
	$self->{handle}->push_write("monitor();\n");
}

sub etpos($self, $value, $cb=undef)
{
	$self->{handle}->push_write("etpos=$value;\n");
	$self->{handle}->push_read(
		line => sub($handle, $line, $eol) {$self->monitor($line, $cb);}
	);
	$self->{handle}->push_write("monitor();\n");
}

sub stop($self)
{
	$self->{handle}->push_write("\x03stop();\n");
}

sub etvel($self, $value, $cb=undef)
{
	$self->{handle}->push_write("etvel=$value;\n");
	# cant monitor cuz cmc "working" doesnt work for velocity
	#$self->{handle}->push_read(
	#	line => sub($handle, $line, $eol) {$self->monitor($line, $cb);}
	#);
	#$self->{handle}->push_write("monitor();\n");
}

sub mtpos($self, $value, $cb)
{
	$self->{handle}->push_read(
		line => sub($handle, $line, $eol) {$self->monitor($line, $cb);}
	);
	$self->{handle}->push_write("mtpos=$value;monitor();\n");
}
sub mpos($self, $cb)
{
	$self->{handle}->push_read(
		line => sub($handle, $line, $eol)
		{
			$cb->($line);
		}
	);
	$self->{handle}->push_write("=mpos;\n");
}

sub epos($self, $cb)
{
	$self->{handle}->push_read(
		line => sub($handle, $line, $eol)
		{
			$cb->($line);
		}
	);
	$self->{handle}->push_write("=epos;\n");
}

# if no push_read events are on the stack,
# then this reader will be called to handle the io.
# we will just add it to the lines array
sub reader($self, $handle)
{
	my $at = CORE::index($handle->{rbuf}, "\n");
	while ($at > -1)
	{
		my $line = substr($handle->{rbuf}, 0, $at+1, '');
		chomp($line);
		push($self->{lines}->@*, $line);
		$at = CORE::index($handle->{rbuf}, "\n");
	}
}

# save our current position to the config
sub savePos($self, $cb)
{
	$self->{handle}->push_read(
		line => sub($handle, $line, $eol)
		{
			#print "SavePos: [$line]\n";
			$self->{cfg}->set(
				'epos',
				$self->{hwaddr},
				$line
			);
			$cb->();
		}
	);
	$self->{handle}->push_write("=epos;\n");
}

sub getSavedPos($self)
{
	return $self->{cfg}->get('epos', $self->{hwaddr});
}

sub disconnect($self)
{
	$self->{handle}->destroy();
	$self->{handle} = 0;
	$self->{fh} = 0;
	$self->{connected} = 0;
	$self->{cfg} = 0;
}


sub connect($self)
{
	$self->{connected} = 0;
	$self->{error} = '';
	my $host = $self->{cfg}->get('csimc', 'HOST') // '127.0.0.1';
	my $port = $self->{cfg}->get('csimc', 'PORT') // 7623;
	tcp_connect(
		$host,
		$port,
		sub {
			$self->{fh} = shift;
			if (! $self->{fh})
			{
				$self->{error} = "csimcd connect failed: $!";
				$self->{connect_event}->();
				return;
			}

			#print "fh = ", $fh->{fh}, "\n";
			$self->{handle} = new AnyEvent::Handle(
				fh			=> $self->{fh},
				rbuf_max	=> 12 * 1024,		# 12k max
				on_error	=> sub($hdl, $fatal, $msg) {
					$self->{error} = "csimcd socket error: $msg";
					$hdl->destroy;
					$self->{handle} = 0;
					$self->{fh} = 0;
					$self->{connected} = 0;
				},
				on_eof		=> sub {
					$self->{handle}->destroy;
					$self->{handle} = 0;
					$self->{fh} = 0;
					$self->{connected} = 0;
				},
				on_read		=> sub { $self->reader(@_) }
			);
			# addr, why=shell=0, zero
			$self->{handle}->push_write( pack('ccc', $self->{hwaddr}, 0, 0) );
			$self->{handle}->push_read( chunk => 1, sub($handle, $data) {
					# I dont think we'll ever use this handle
					# we'll print it, but otherwise toss it
					my $result = unpack('C', $data);
					$self->{connected} = 1;
					$self->{connect_event}->("connect handle: $result");
				}
			);
		}
	);
}


1;
