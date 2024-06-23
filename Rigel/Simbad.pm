package Simbad;
use common::sense;
use Net::Curl::Easy qw(:constants);
use DBI;
use feature 'signatures';
use Time::HiRes qw( gettimeofday tv_interval );
use Scalar::Util qw( looks_like_number );
use Astro::Coords;
use Data::Dumper;
use FindBin qw($Bin);

# package to search for star's and return RA/Dec.
# We query a local mirror of http://simbad.u-strasbg.fr/simbad
# Also keeps a local cache in stars.sqlite.
# libsqlitefunctions will be linked, it provies sqrt and square
# which we use to order by distance

my $debug = 0;

sub new
{
	my $class  = shift;
	my $obj = {
		curl	=> Net::Curl::Easy->new(),
		db		=> opendb(),
		@_
	};
	my $c = $obj->{curl};
	$c->setopt(CURLOPT_SSL_VERIFYPEER, 0 );
	$c->setopt(CURLOPT_USERAGENT, "Net::Curl/$Net::Curl::VERSION");
	#$c->setopt(CURLOPT_VERBOSE, 1 );
	$c->setopt(CURLOPT_CONNECTTIMEOUT, 9);
	$c->setopt(CURLOPT_ACCEPT_ENCODING, '');  # accept any
	$c->setopt(CURLOPT_FOLLOWLOCATION, 1);
	$c->setopt(CURLOPT_MAXREDIRS, 4);

	return bless $obj, $class;
}

sub opendb()
{
	my $tmp = "$Bin/stars.sqlite";
	if (! -r $tmp)
	{
		print "Star database not found: $tmp\n";
		return 0;
	}
	my $db = DBI->connect("dbi:SQLite:dbname=$tmp");
	$db->sqlite_enable_load_extension(1);
	$tmp = "$Bin/libsqlitefunctions";
	print "Load: $tmp\n";
	$db->sqlite_load_extension($tmp);
	$db->do('pragma cache_size=-4096');
	#$db->do('pragma locking_mode=exclusive');

	return $db;
}

# you have to call createdb if you want a new one, wont be called by default
sub createdb($path)
{
	my $tmp;
	if ($path) {
		$tmp = "$path/stars.sqlite";
	} else {
		$tmp = 'stars.sqlite';
	}
	my $db = DBI->connect("dbi:SQLite:dbname=$tmp");
	die "this isnt up to date";
	$db->do('create table if not exists catalog(catid integer primary key, name text not null)');
	$db->do('create table if not exists star(starid integer primary key, ra float, dec float, type text, plx float, pmra float, pmdec float, radial float, redshift float, spec text, bmag float, vmag float)');
	$db->do('create table if not exists lookup(starid integer, catid integer, id text)');
	$db->do('create index if not exists lookup_star on lookup(starid)');
	$db->do('create index if not exists lookup_id on lookup(id)');
	$db->do('create index if not exists lookup_catid on lookup(catid)');
	$db->do('create index if not exists catalog_name on catalog(name)');
	return $db;
}

sub query($self, $id)
{
	my $t0 = [gettimeofday];
	my $c = $self->{curl};
	my $limit = "set limit 3000\n";
	$c->setopt(CURLOPT_HTTPGET, 1);
	#my $url = 'http://simbad.u-strasbg.fr/simbad/sim-script?script=' . $c->escape("output console=off script=off\n"
	if (ref($id) eq 'ARRAY')
	{
		$id = join("\n", @$id);
		$limit = '';
	} else {
		$id = "query $id\n";
	}
	my $url = 'http://simbad.harvard.edu/simbad/sim-script?script=' . $c->escape("output console=off script=off\n"
		. 'format object "%COO(d;A)\n'
		. '%COO(d;D)\n'
		. '%OTYPELIST\n'
		. '%PLX(V)\n'
		. '%PM(A)\n'
		. '%PM(D)\n'
		. '%RV(V)\n'
		. '%RV(Z)\n'
		. '%SP(S)\n'
		. '%FLUXLIST(B)[%flux(F)]\n'
		. '%FLUXLIST(V)[%flux(F)]\n'
		. '%IDLIST()=====\n"'
		. "set epoch J2000\n"
		. $limit
		. $id
	);
	#. query around nunki radius=10m
	#. "query cat NGC\n"

	$c->setopt( CURLOPT_URL, $url);
	my $resp;
	$c->setopt(CURLOPT_WRITEDATA,  \$resp);
	eval { $c->perform(); };
	if ($@) {
		die "curl failed: $@";
	}
	my $result = {
		"webq" =>  tv_interval ( $t0, [gettimeofday])
	};
	my $x = $c->getinfo(CURLINFO_HTTP_CODE);
	if ($x == 200)
	{
		if ($debug >= 2)
		{
			print "Debug: saved to dump.star\n";
			open(F, '>', 'dump.star');
			print F $resp;
			close(F);
		}
		open(my $inf, '<', \$resp);
		$self->saveStar($inf, $result);
		close($inf);
	} else {
		print "web result: $x\n$resp\n" if $debug;
		$result->{'err'} = $x;
		$result->{'descr'} = $resp;
	}
	return $result;
	#print "Result Code: $x\n[$resp]\n";
}

sub splitName($id)
{
	# my ($catalog, $id) = split(/\-|\+|\s+/, $names[0], 2);
	# my ($catalog, $newid) = split(/\s+/, $id, 2);
	my($at, $catalog, $newid);
	if ($id =~ /^\s*\[/)
	{
		# if first thing is a [, use  [] as category name
		$at = index($id, ']');
		if ($at == -1)
		{
			die "splitName found [ but not ]: $id at";
		}
		$at++;
	}
	elsif ($id =~ /\-|\+|\s/g)
	{
		$at = pos($id) - 1;
		#print "split at $at\n";
	}
	else
	{
		#print "cat didnt split [$id]\n";
		if (index($id, 'OGLE') == 0)
		{
			$at = 4;
		}
		elsif (index($id, 'ZTF') == 0)
		{
			$at = 3;
		}
		elsif (index($id, 'DES') == 0)
		{
			$at = 3;
		}
		elsif (index($id, 'SPIRITS') == 0)
		{
			$at = 7;
		}
		else
		{
			die "nothing found to split on [$id] at";
		}
	}
	$catalog = substr($id, 0, $at);
	for ($catalog)
	{
		s/^\s+//;
		s/\s+$//;
	}
	$newid = substr($id, $at);
	for ($newid)
	{
		s/^\s+//;
		s/\s+$//;
		s/\s{2,}/ /g;   #remove double+ spacing
	}
	return ($catalog, $newid);
}


sub saveStar($self, $inf, $result)
{
	my $t0 = [gettimeofday];
	my $db = $self->{db};
	return if (! $db);
	if (! $self->{intrans})
	{
		$db->begin_work;
	}
	my $insStar = $db->prepare_cached('insert into star(ra, dec, type, plx, pmra, pmdec, radial, redshift, spec, bmag, vmag) '
		. ' values (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?) '
	);
	my $findCat = $db->prepare_cached('select catid from catalog where name = ? ');
	my $insCat = $db->prepare_cached('insert into catalog(name) values (?)');
	my $insLookup = $db->prepare_cached('insert into lookup(starid, catid, id) values (?,?,?)');
	my $findStar = $db->prepare_cached('select starid from lookup where catid = ? and id = ? ');

	my $ccSave = 0;
	my $ccSkip = 0;
	while (! eof($inf))
	{
		my ($line, $x);
		my @params = ();
		for $x (0..10)
		{
			$line = <$inf>;
			chomp($line);
			push(@params, $line);
		}
		last if (eof($inf));

		my @names = ();
		while (! eof($inf))
		{
			$line = <$inf>;
			chomp($line);
			last if ($line eq '=====');
			push(@names, $line);
		}

		# must have names
		next if (scalar(@names) == 0);

		# must have numeric ra and dec
		for $x (@params)
		{
			$x =~ s/\s+$//;
			$x = undef if ($x eq '~');
		}
		if (!looks_like_number($params[0]) or !looks_like_number($params[1]))
		{
			$ccSkip++;
			next;
		}

		my ($catalog, $id) = splitName($names[0]);
		if (! $catalog)
		{
			print "got names, but no catalog:\nName = ";
			print Dumper(\@names);
			print "\ncatalog = [$catalog], id = [$id]\n";
			die;
		}
		print "cat = [$catalog] id = [$id] " if $debug;
		if ($catalog eq 'YZ')
		{
			# YZ catalog has dups, skip it
			die 'YZ catalog cannot be primary';
		}
		$findCat->execute($catalog);
		my($catid) = $findCat->fetchrow_array;
		if (! $catid)
		{
			print "  Adding catalog $catalog\n";
			$insCat->execute($catalog);
			$catid = $db->sqlite_last_insert_rowid();
		}
		print " catid [$catid] " if $debug;

		#see if star exists
		$findStar->execute($catid, $id);
		my($starid) = $findStar->fetchrow_array();
		if ($starid) {
			print " (skipped)\n" if $debug;
			$ccSkip++;
			next;
		}

		# save it
		$insStar->execute(@params);
		$starid = $db->sqlite_last_insert_rowid();

		for my $alt (@names)
		{
			my ($catalog, $id) = splitName($alt);
			next if ($catalog eq 'YZ');
			$findCat->execute($catalog);
			my($catid) = $findCat->fetchrow_array;
			if (! $catid)
			{
				$insCat->execute($catalog);
				$catid = $db->sqlite_last_insert_rowid();
			}
			$insLookup->execute($starid, $catid, $id);
		}
		print " (saved)\n" if $debug;
		$ccSave++
	}
	$insStar->finish();
	$findCat->finish();
	$insCat->finish();
	$insLookup->finish();
	$findStar->finish();
	if (! $self->{intrans})
	{
		$db->commit;
	}
	$result->{'saved'} = $ccSave;
	$result->{'skipped'} = $ccSkip;
	$result->{"saveStar"} = tv_interval ( $t0, [gettimeofday]);
}

sub findLocal($self, $catalog, $id)
{
	my $t0 = [gettimeofday];
	my $db = $self->{db};
	return 0 if (! $db);
	if ($catalog eq 'coord')
	{
		my $by = 0.1;
		my $ra = $id->ra(format => 'dec' );
		my $dec = $id->dec(format => 'dec' );
		# given a point, search for things near that
		# point, order by distance, and return one record
		my $q = $db->prepare(qq{select
sqrt(square(ra - $ra) + square(dec - $dec)) as dist,
star.*
from star
where ra between ? and ?
and dec between ? and ?
order by 1
limit 1
}
		);

		$q->execute($ra - $by, $ra + $by, $dec - $by, $dec + $by);

		my $row = $q->fetchrow_hashref;
		$q = undef;
		print "FindLocal: ", tv_interval($t0), "\n";
		return $row;

	}
	else
	{
		my $q = $db->prepare(q{select star.*
from star
inner join lookup on lookup.starid = star.starid
inner join catalog on catalog.catid = lookup.catid
where catalog.name = ? and lookup.id = ?}
		);
		$q->execute($catalog, $id);
		my $row = $q->fetchrow_hashref;
		if (! $row)
		{
			my $result = $self->query("around $catalog $id radius=20m");
			print "saved: $result->{saved} skipped: $result->{skipped} web: $result->{webq} db: $result->{saveStar}\n";
			$q->execute($catalog, $id);
			$row = $q->fetchrow_hashref;
		}
		$q = undef;
		print "FindLocal: ", tv_interval($t0), "\n";
		return $row;
	}
}

1;

