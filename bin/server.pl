#!/usr/bin/perl

use common::sense;
use feature 'signatures';
use AnyEvent;
use AnyEvent::Strict;
#use AnyEvent::Handle;
#use AnyEvent::SerialPort;
use AnyEvent::HTTPD;
#use AnyEvent::Socket;
use AnyEvent::ReadLine::Gnu;
use Text::Xslate qw(mark_raw);
use FindBin qw($Bin);
use Data::Dumper;
use HTTP::XSCookies qw/bake_cookie crush_cookie/;
use JSON::XS;
use Astro::PAL;
use Astro::Coords;
use Astro::Telescope;
use PDL;
use PDL::Core::Dev;
use Cwd 'abs_path';
use Proc::ProcessTable;
use DateTime;
use RPi::WiringPi;
use RPi::Const qw(:all);
use lib "$Bin/..";
use Rigel::Config;
use Rigel::Stellarium;
use Rigel::LX200;
use Rigel::Simbad;
use Rigel::Csi;
use Rigel::Dome;
#use Memory::Usage;

# The server is event based, read up on AnyEvent for details.
# The program will listen on a bunch of different ports/sockets
# and fire events when something interesting happens.
# Look for things like on_read


# path's are relative to the server.pl script, set it as
# our cwd
BEGIN { chdir($Bin); }

use Inline CPP => config =>
	libs	=> '-lqsiapi -lcfitsio -lftdi1 -lusb-1.0',
	ccflags => '-std=c++11 -I/usr/include/libftdi1 -I/usr/include/libusb-1.0',
	INC           => &PDL_INCLUDE,
    TYPEMAPS      => &PDL_TYPEMAP,
    AUTO_INCLUDE  => &PDL_AUTO_INCLUDE,
    BOOT          => &PDL_BOOT;

use Inline 'CPP' => '../Rigel/camera.cpp';

#my $mu = Memory::Usage->new();
#$mu->record('startup');

my $pi = RPi::WiringPi->new;
my $p17 = $pi->pin(17);
$p17->mode(OUTPUT);

my ($camera, $httpd, $ra, $dec, $focus, $dome, $lx2);

my $esteps = 12976128;
my $stepsPerDegree = $esteps / 360;

my $cfg = Rigel::Config->new();
$cfg->set('app', 'template', abs_path('../template'));

if (! -d '/tmp/cache')
{
	mkdir('/tmp/cache') or die;
}
my $tt = Text::Xslate->new(
	path		=> $cfg->get('app', 'template'),
	cache_dir	=> '/tmp/cache',
	syntax		=> 'Metakolon'
);

# we will pretend to be an lx200 mount.
# these are the command we receive and functions to call upon receipt
my %lxCommands = (
	':Aa#'	=> \&lxStartAlignment,
	':Ga#'	=> \&lxGetTime12,
	':GC#'	=> \&lxGetDate,
	':Gc#'	=> \&lxGetDateFormat,
	':GD#'	=> \&lxGetDec,
	':GL#'	=> \&lxGetTime24,
	':GR#'	=> \&lxGetRA,
	':GS#'	=> \&lxGetSTime,
	':GVP#'	=> \&lxGetName,
	':GZ#'	=> \&lxGetAzimuth,
	':hS#'	=> \&lxGoHome,
	':hF#'	=> \&lxGoHome,
	':hP#'	=> \&lxGoHome,
	':h?#'	=> \&lxHomeStatus,
	':Me#'	=> \&lxSlewEast,
	':Q#'	=> \&AllStop
);

# This is where the rigel telescope is located.
# some of the Astro::Coords functions need it.
my $telescope = new Astro::Telescope(
	Name => 'Rigel',
	Long => -91.500299 * DD2R,
	Lat  => 41.889387 * DD2R,   #decimal degrees to radians
	Alt  => 246.888  # metres
);

# Simbad is where we'll lookup star name
# http://simbad.u-strasbg.fr/simbad
my $simbad = Simbad->new();

# also listen for Stellarium network commands.
my $stSocket = new Rigel::Stellarium( recv => \&stCommand );

# for debugging, also present a command prompt
my $term = AnyEvent::ReadLine::Gnu->new(
	prompt	=> "rigel> ",
	on_line	=> \&processCmd
);

main();
exit 0;

sub initCsi
{
	$term->hide();

	# open usb ports and autodetect whats plugged in
	$cfg->findPorts();

	$dome->connect($cfg->get('dome', 'TTY'));

	$camera = new Camera();
	print($camera->getInfo(), "\n");

	# if csimcd is already running, lets kill it and start
	# over fresh.  Return true if fake.pl is running
	my $testing = killCsimcd();

	# only start daemon if we auto detected serial port connection
	my $tmp = $cfg->get('csimc', 'TTY');
	if ($tmp || $testing)
	{
		print("connecting to ",$cfg->get('csimc', 'HOST'),':', $cfg->get('csimc', 'PORT'), "\n");
		print("loading csimc scripts...\n");
		# -r reboot, -l load scripts.
		# should start csimcd and load the *.cmc scripts.
		# wont return untill everything is ready
		system('./csimc -l < /dev/null');

		# each control (ra, dec, focus) gets its own connection
		$ra = Csi->new(
			cfg => $cfg,
			hwaddr => 0,
			connect_event => sub($msg)
			{
				if ($msg) {
					$term->print("RA: $msg\n");
					my $st = $ra->getSavedPos();
					$term->print("RA saved pos: $st\n");
				} else {
					$term->print("RA: ", $ra->{error}, "\n");
				}
			}
		);
		$dec = Csi->new(
			cfg => $cfg,
			hwaddr => 1,
			connect_event => sub($msg)
			{
				if ($msg) {
					$term->print("DEC: $msg\n");
					my $st = $dec->getSavedPos();
					$term->print("DEC saved pos: $st\n");
				} else {
					$term->print("DEC: ", $ra->{error}, "\n");
				}
			}
		);
		$focus = Csi->new(
			cfg => $cfg,
			hwaddr => 2,
			connect_event => sub($msg)
			{
				if ($msg) {
					$term->print("Focus: $msg\n");
				} else {
					$term->print("Focus: ", $ra->{error}, "\n");
				}
			}
		);
	}
	$term->show();
}


sub main
{
	if (-r '/dev/ttyS0')
	{
		$lx2 = Rigel::LX200->new( port => '/dev/ttyS0', recv => \&lxCommand );
		if ($lx2) {
			$term->print("lx200 client listening on /dev/ttyS0\n");
		}
	}
	# create our web server
	$httpd = AnyEvent::HTTPD->new(
		host => '::',
		port => 9090,
		request_timeout => 5
	);
	$httpd->reg_cb(
		error => sub {
			my($e) = @_;
			$term->print("httpd error: $e\n");
		},
		request => \&webRequest
	);
	$term->print("web server on port 9090\n");

	$dome = Dome->new();
	if (isPowerOn())
	{
		initCsi();
	}
	else
	{
		$term->print("power is off\n");
	}

	my $t;
	$t = AnyEvent->timer(
		after => 3,
		interval => 3,
		cb => sub {
			#$term->print("3-sec timer\n");
			if ($cfg->checkMonitor())
			{
				# usb add/removed
			}
		}
	);

	my $t2;
	$t2 = AnyEvent->signal(
		signal => "INT",
		cb => sub {
			$term->print("SIGINT!  Stopping all movement\n");
			allStop();
		}
	);

	#$mu->record('ready');
	#$mu->dump();

	$httpd->run();  #start event loop, never returns
}

sub getStatus($sub)
{
	my $data = {
		status => 'telescope stuff'
	};

	my $wait = AnyEvent->condvar;

	# begin1: this will set the procedure
	# that will be called when the last async end fires
	$wait->begin(sub{
		$data->{ra} = $data->{raEncoder} / $stepsPerDegree;
		$data->{dec} = $data->{decEncoder} / $stepsPerDegree;

		$sub->($data)
	});

	$wait->begin;  # begin2
	$ra->epos( sub($val) {
		$term->print("get RA: [$val]\n");
		$data->{raEncoder} = int($val);
		$wait->end; # end1
	});

	$wait->begin; # begin3
	$dec->epos( sub($val) {
		$term->print("get DEC: [$val]\n");
		$data->{decEncoder} = int($val);
		$wait->end; # end1
	});

	$wait->end;  #end3
}

sub webRequest($httpd, $req)
{
	# $term->print(Dumper($req->headers));
	my $c = $req->headers->{cookie};
	if ($c ) {
		my $values = crush_cookie($c);
		#print 'cookie: ', Dumper($values), "\n";
	}

	if ($req->method eq 'POST')
	{
		my %v = $req->vars;
		my $buf = "<html><body>name = " . $req->parm('name') . '<br> method = ' . $req->method
			. '<br>path = ' . $req->url->path
			. '<br>vars = ' . Dumper(\%v);

		$req->respond ({ content => ['text/html', $buf]});
		return;
	}

	my $path = $req->url->path;
	if ($path eq '/west')
	{
		$term->print("go west\n");
		slewWest();
		return sendJson($req, {});
	}
	if ($path eq '/east')
	{
		$term->print("go east\n");
		slewEast();
		return sendJson($req, {});
	}
	if ($path eq '/stop')
	{
		$term->print("Stop\n");
		allStop();
		return sendJson($req, {});
	}
	if ($path eq '/status')
	{
		return getStatus(
			sub
			{
				my($data) = @_;
				# cpos = (2*PI) * mip->esign * draw / mip->estep;
				return sendJson($req, $data);
			}
		);
	}

	my $t = $cfg->get('app', 'template');
	if ($path eq '/') {
		$path = '/index.html';
	}

	my $file = abs_path($t . $path);

	if ($file !~ /^$t/)
	{
		$term->print("uri [$path] -> [$file]: 404 bad path\n");
		$req->respond([404, 'not found', { 'Content-Type' => 'text/html' }, 'Bad Path']);
		return;
	}


	if ( -e $file )
	{
		my($buf, $ctype);
		if ($file =~ /\.css$/)
		{
			open(F, '<', $file);
			local $/ = undef;
			$buf = <F>;
			close(F);
			$ctype = 'text/css; charset=utf-8';
		} elsif ($file =~ /\.js$/) {
			open(F, '<', $file);
			local $/ = undef;
			$buf = <F>;
			close(F);
			$ctype = 'application/javascript; charset=utf-8';
		} else {
			$buf = $tt->render($path);
			$ctype = 'text/html; charset=utf-8';
		}
		my $cookie = bake_cookie('baz', {
				value   => 'Frodo',
				expires => '+11h'
		});
		$req->respond([
			200, '',
			{'Content-Type' => $ctype,
			'Content-Length' => length($buf),
			'Set-Cookie' => $cookie
			},
			$buf
		]);
		$term->print("uri [$path] -> [$file]: 200 ok\n");
		#$mu->record('web');
		#$mu->dump();
		return;
	}
	else
	{
		$term->print("uri [$path] -> [$file]: 404 not found\n");
		$req->respond([404, '', { 'Content-Type' => 'text/html' }, 'Sorry, file not found']);
		return;
	}
}


sub raReader($handle)
{
	state $buffer = '';
	$buffer .= $handle->{rbuf};

	$handle->{rbuf} = '';
	my $at = CORE::index($buffer, "\n");
	while ($at > -1)
	{
		my $line = substr($buffer, 0, $at+1, '');
		$term->print("RA: ", $line);
		$at = CORE::index($buffer, "\n");
	}
}

sub decReader($handle)
{
	my $at = CORE::index($handle->{rbuf}, "\n");
	while ($at > -1)
	{
		my $line = substr($handle->{rbuf}, 0, $at+1, '');
		$term->print("DEC: ", $line);
		$at = CORE::index($handle->{rbuf}, "\n");
	}
}


sub sendJson($req, $data, $cookie=0)
{
	my $buf = encode_json($data);
	my $headers = {
		'Content-Type' => 'application/json; charset=utf-8',
		'Content-Length' => length($buf)
	};
	if ($cookie)
	{
		$headers->{'Set-Cookie'} = bake_cookie('baz', $cookie);
	}
	$req->respond([200, '', $headers, $buf]);
}

sub stCommand($coords)
{
	$term->print("Main stCommand\n");

	$coords->telescope($telescope);

	my($ra, $dec, $star);

	$ra = $coords->ra(format => 'dec' );
	$dec = $coords->dec(format => 'dec' );
	$term->print( "J2000     RA: $ra, Dec: $dec\n");
	$term->print( "-- Database:\n");
	$star = $simbad->findLocal('coord', $coords);
	$term->print( Dumper($star));

	$term->print( "-- Status:\n", $coords->status, "\n");
}

sub lxCommand($cmd, $handle)
{
	my $f = $lxCommands{$cmd};
	if ($f) {
		$term->print("lxCommand: [$cmd]\n");
		$f->($handle);
	} else {
		$term->print("unknown lxCommand: [$cmd]\n");
	}
}


sub lxStartAlignment($handle)
{
	# not needed, return true
	$handle->push_write('1');
}
sub lxHomeStatus($handle)
{
	$handle->push_write('1');  # home found
}

sub lxGoHome($handle)
{
	$term->print("Home requested ...  I'll just fake it\n");
	#initHome();
}

sub lxSlewWest($handle)
{
	$ra->etvel(-15000);
	$term->print("ok, going west\n");
}

sub lxSlewEast($handle)
{
	$ra->etvel(15000);
	$term->print("ok, going east\n");
}

sub lxGetTime12($handle)
{
	my $time = DateTime->now;
	my $buf = $time->strftime('%I:%M:%S');
	$buf .= '#';
	$handle->push_write($buf);
}
sub lxGetTime24($handle)
{
	my $time = DateTime->now;
	my $buf = $time->strftime('%H:%M:%S');
	$buf .= '#';
	$handle->push_write($buf);
}
sub lxGetDate($handle)
{
	my $time = DateTime->now;
	my $buf = $time->strftime('%m/%d/%y');
	$buf .= '#';
	$handle->push_write($buf);
}
sub lxGetDateFormat($handle)
{
	$handle->push_write('12#');
}

# from: https://metacpan.org/pod/Geo::Coordinates::DecimalDegrees
sub decimal2dms {
    my ($decimal) = @_;

    my $sign = $decimal <=> 0;
    my $degrees = int($decimal);

    # convert decimal part to minutes
    my $dec_min = abs($decimal - $degrees) * 60;
    my $minutes = int($dec_min);
    my $seconds = ($dec_min - $minutes) * 60;

    return ($degrees, $minutes, $seconds, $sign);
}

sub lxGetAzimuth($handle)
{
	getStatus(sub($data){
    	my $x = new Astro::Coords::Angle( $data->{ra}, units => 'deg', range => '2PI' );
		$x->str_ndp(0);
		$handle->push_write("$x#");
	});
}
sub lxGetRA($handle)
{
	getStatus(sub{
		my($data) = @_;
    	my $x = new Astro::Coords::Angle( $data->{ra}, units => 'deg', range => '2PI' );
		$x->str_ndp(0);
		$handle->push_write("$x#");
	});
}

sub lxGetDec($handle)
{
	getStatus(sub{
		my($data) = @_;
    	my $x = new Astro::Coords::Angle( $data->{dec}, units => 'deg', range => '2PI' );
		$x->str_ndp(0);
		$handle->push_write("$x#");
	});
}
sub lxGetSTime($handle)
{
	my $time = DateTime->now( time_zone => 'UTC' );

	# Get the longitude (in radians)
	my $long = $telescope->long ;

	# Seconds can be floating point.
	my $sec = $time->sec;

	my $lst = (Astro::PAL::ut2lst( $time->year, $time->mon,
			$time->mday, $time->hour,
			$time->min, $sec, $long))[0];

	my $obj = new Astro::Coords::Angle::Hour( $lst, units => 'rad', range => '2PI');
	$obj->str_ndp(0);

	my $buf = $obj->in_format('s');
	if ($handle)
	{
		$buf .= '#';
		$handle->push_write($buf);
	}
	else
	{
		$term->print("Current Sidereal Time: ", $buf, "\n");
	}
}
sub lxGetName($handle)
{
	$handle->push_write("Rigel#");
}

sub allStop()
{
	$ra->stop();
	$dec->stop();
	$focus->stop();
}

sub processCmd($cmd, $length)
{
	$term->hide;
	given ($cmd)
	{
		when ('exit')
		{
			print("bye\n");
			exit 0;
		}
		when ('help')
		{
			showHelp();
		}
		when ('stop')
		{
			print "Stopping...\n";
			allStop();
		}
		when (['s', 'status'])
		{
			getStatus(sub{
				my($data) = @_;
				$term->print(Dumper($data));
				my $x = new Astro::Coords::Angle( $data->{dec}, units => 'deg', range => '2PI');
				$x->str_ndp(0);
				$term->print("[$x]\n");
			});
		}
		when (/^find\s+(\w+)\s+(\w+)/)
		{
			findStar($1, $2);
		}
		when (/^go\s+(\w+)\s+(.*)/)
		{
			moveMount($1, $2);
		}
		when ('home')
		{
			initHome();
		}
		when ('report')
		{
			$term->print("no report yet\n");
			#$ra->push_write("stats();\n");
		}
		when ('picture')
		{
			takePicture();
		}
		when ('poweron')
		{
			powerOn();
		}
		when ('poweroff')
		{
			powerOff();
		}
		when (/^focus\s*(\d+)?/)
		{
			focus($1);
		}
		default
		{
			print("CMD: [$cmd]\n");
		}
	}
	$term->show;
}

sub powerOff()
{
	if (isPowerOn())
	{
		my $wait = AnyEvent->condvar;
		$wait->begin(sub{
			$ra->disconnect();
			$dec->disconnect();
			$focus->disconnect();
			killCsimcd();
			$cfg->clear();
			$ra = 0;
			$dec = 0;
			$focus = 0;
			$p17->write(HIGH);
			$term->print("power off\n");
		});

		$wait->begin();
		$ra->savePos( sub{$wait->end;} );

		$wait->begin();
		$dec->savePos( sub{$wait->end;} );

		$wait->begin();
		$focus->savePos( sub{$wait->end;} );

		$camera = 0;
		$dome->disconnect();
		$wait->end;
	}
	else
	{
		print "power is already off\n";
	}
}

sub powerOn()
{
	if (isPowerOn())
	{
		print "power is already on\n";
	}
	else
	{
		print "wait 5 seconds for poweron...\n";
		$p17->write(LOW);
		# give things a sec to come up
		sleep(5);
		initCsi();
	}
}

sub isPowerOn()
{
	return $p17->read() == LOW;
}


sub moveMount($dir, $amount)
{
	my $ex = int( $amount * $stepsPerDegree );
	print "move [$ex]\n";
	given ($dir)
	{
		when ('east')
		{
			$ra->etpos_offset($ex);
		}
		when ('west')
		{
			$ra->etpos_offset(-$ex);
		}
		when ('up')
		{
			$dec->etpos_offset($ex);
		}
		when ('down')
		{
			$dec->etpos_offset(-$ex);
		}
	}
}

sub findStar($cat, $id)
{
	print "Search [$cat] for [$id]\n";
	my $star = $simbad->findLocal($cat, $id);
	if (! $star)
	{
		print "Star not found\n";
	}
	else
	{
		my $cc = new Astro::Coords(
			name => "$cat $id",
			ra   => $star->{ra},
			dec  => $star->{dec},
			type => 'J2000',
			units=> 'degrees',
			tel => $telescope
		);
		# print $cc->status, "\n";
		print "Apparent  RA: ", $cc->ra_app( format => 'd'), " deg\n";
		print "Apparent Dec: ", $cc->dec_app( format => 'd'), " deg\n";
		print "  Hour angle: ", $cc->ha( format => "d"), " deg\n";
		print "     Azimuth: ", $cc->az( format => 'd'), " deg\n";
	}
}


sub showHelp
{
	$term->print( <<EOS );
Commands:
exit
help
status
stop
home
find catalog id
go up x
go down x
go east x
go west x
poweron
poweroff
picture
focus [x] (0-25436)

EOS
}

sub killCsimcd
{
	my $pt = Proc::ProcessTable->new();
	for my $p ( @{$pt->table} )
	{
		if ($p->fname eq 'csimcd')
		{
			$p->kill('TERM');
			return 0;
		}
		elsif ($p->cmdline->[1] =~ /fake.pl/)
		{
			return 1;
		}
	}
	return 0;
}

sub initHome()
{
	#$ra->push_write("findhome(1);\n");
	#$dec->push_write("findhome(1);\n");
}

sub takePicture()
{
	$camera->takePicture();
}

sub focus($opt)
{
	$focus->mpos( sub($val) {
		$term->print("focus mpos: [$val]\n");
		if ($opt)
		{
			$term->print("Setting focus to: $opt\n");
			$focus->mtpos($opt, sub {
				$term->print($focus->{status}, "\r");
				if (! $focus->{working})
				{
					$term->print("\n");
				}
			});
		}
	});
}


