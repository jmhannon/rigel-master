function go(dir) {
	$.getJSON('/'+dir, function(data) {
		if (dir === 'stop') {
			clearInterval()
		} else {
			setInterval(getStatus, 2000);
		}
		console.log("setTimeout");
	});
}

function getStatus() {
	console.log('getStatus');
	$.getJSON('/status', function(data) {
		document.getElementById('status').innerHTML = data.status
			+ '<br>RA: ' + data.ra
			+ '<br>DEC: ' + data.dec;
	});
}
