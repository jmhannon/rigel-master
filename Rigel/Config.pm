package Rigel::Config;

use common::sense;
use feature 'signatures';
use Device::SerialPort;
use Udev::FFI;
use Time::HiRes 'sleep';

# This unit is not just config stuff, it will also
# probe ports to try and find what thing is plugged
# into what port.


sub new($class)
{
	my $self = {
		modified => 0,
	};
	bless($self, $class);

	my $udev = Udev::FFI->new() or
		die "Can't create Udev::FFI object: $@";

	my $monitor = $udev->new_monitor() or
		die "Can't create udev monitor: $@.\n";

	$monitor->filter_by_subsystem_devtype('tty');
	$monitor->start();
	$self->{udev} = $udev;
	$self->{monitor} = $monitor;
	$self->clear();

	return $self;
}

sub set($self, $app, $key, $value)
{
	my $found = 0;
	my @lines;
	my $fname = "config/$app.cfg";
	if (open(my $f, '<', $fname))
	{
		@lines = <$f>;
		close($f);

		foreach my $line (@lines)
		{
			# just match key part
			if ($line =~ /^\s*$key(?:=|\s)/ )
			{
				#print "SetFound [$key] \n";
				$line = "$key = $value\n";
				$found = 1;
				last;
			}
		}
	}

	if (! $found)
	{
		push(@lines, "$key = $value\n");
	}
	if (open(my $f, '>', $fname))
	{
		print $f @lines;
		close($f);
	}

}

sub get($self, $app, $key)
{
	if (open(my $f, '<', "config/$app.cfg"))
	{
		while (my $line = <$f>)
		{
			if ($line =~ /^\s*$key\s*(?:=|\s)\s*(.*)$/ )
			{
				my $val = $1;
				# Remove inline comments.
				$val =~ s/\s*\!.+$//g;
				$val =~ s/\s*\#.+$//g;
				# Remove quotes
				$val =~ s|^\"(.*)\"$|$1|s;
				$val =~ s|^\'(.*)\'$|$1|s;

				return $val;
			}
		}
	}
	else
	{
		print "file not found: config/$app.cfg\n";
	}

	return undef;
}

sub clear($self)
{
	# blank it, assume auto detect works.
	$self->set('csimc', 'TTY', '');
	$self->set('dome', 'TTY', '');
}

# open and probe ports and see if we
# can figure out whats plugged in.
sub findPorts($self)
{
	$self->clear();
	my @list = glob('/dev/ttyUSB*');
	for my $dev (@list)
	{
		my $found = 0;
		print "Testing port: $dev\n";

		my $tty = Device::SerialPort->new($dev);
		if (! $tty)
		{
			print "Cant open: $!\n";
			next;
		}

		# first try to detect csimc
		$tty->baudrate(38400);
		$tty->databits(8);
		$tty->parity("none");
		$tty->stopbits(1);
		$tty->handshake("none");
		if (! $tty->write_settings)
		{
			print "Error writing settings\n";
			$tty = undef;
			next;
		}
		$tty->purge_all;
		$tty->lookclear();
		$tty->read_char_time(0);     # don't wait for each character
		$tty->read_const_time(500);  # 0.5 second per unfulfilled "read" call

		# send the csimc ping
		my $buf = pack('CCCCCC', 0x88, 0x00, 0x20, 0x16, 0x00, 0xbe);

		# dump it for deubgging
		#open(F, '>', 'out');
		#print F $buf;
		#close(F);

		my $x = $tty->write($buf);
		#print "wrote $x bytes\n";
		sleep(0.50);  # seconds
		my ($count, $buf) = $tty->read(1);
		#print "read count $count\n";
		if ($count == 1)
		{
			my $x = unpack('C', $buf);
			#print "::$x\n";
			if ($x == 0x88)
			{
				$self->set('app', $dev, 'csimc');
				# it responded with the right packet type, so assume we found it
				$self->set('csimc', 'TTY', $dev);
				$found = 1;
				print "Its the csimc\n";

				# there are 5 more bytes to the packet
				#my ($count, $buf) = $tty->read(5);
				#if ($count == 5)
				#{
				#	my ($to, $from, $syn, $count, $crc) = unpack('C*', $buf);
					#print "got a good packet\n";
				#}
				last;
			}
		}

		if ($found)
		{
			$tty = undef;
			next;
		}

		# Now try the dome
		$tty->baudrate(9600);
		$tty->databits(8);
		$tty->parity("none");
		$tty->stopbits(1);
		$tty->handshake("none");
		if (! $tty->write_settings)
		{
			print "Error re-writing settings\n";
			$tty = undef;
			next;
		}
		$tty->purge_all;
		$tty->lookclear();
		$tty->read_char_time(0);
		$tty->read_const_time(1000);  #give it a second
		$tty->write('GINF');
		#expect V\d.*?\n\n
		my ($count, $buf) = $tty->read(255);
		if ($buf =~ /V\d+,/)
		{
			#close enough
			$self->set('dome', 'TTY', $dev);
			$tty = undef;
			$self->set('app', $dev, 'dome');
			print "Its the dome\n";
			next;
		}
	}
}

# usb interface to detect when something is
# added or removed.
sub checkMonitor($self)
{
    my $device = $self->{monitor}->poll(0.1);
	return 0 unless ($device);

	my $dev = $device->get_devnode();
	if ($device->get_action() eq 'add')
	{
		$self->set('app', $dev, 'lx200');
		$self->set('app', 'lx200', $dev);
	}
	else # $device->get_action() eq 'remove'
	{
		$self->set('app', $dev, '');
		$self->set('app', 'lx200', '');
	}

    print 'ACTION: '.$device->get_action()."\n";
	#print 'DevPath '.$device->get_devpath()."\n";
    print 'Subsystem '.$device->get_subsystem()."\n";
	#print 'DevType '.$device->get_devtype()."\n";
	#print 'SysPath: '.$device->get_syspath()."\n";
	#print 'SysName '.$device->get_sysname()."\n";
	#print 'Sysnum '.$device->get_sysnum()."\n";
    print 'DevNode '.$device->get_devnode()."\n";
	#print 'Driver '.$device->get_driver()."\n";
	#print 'devnum '.$device->get_devnum()."\n";
	#print 'udev '.$device->get_udev()."\n";
	return 1;
}

1;
