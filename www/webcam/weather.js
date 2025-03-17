function updateMonitorix(when) {
	var request = new XMLHttpRequest();
	if (typeof when == 'undefined')
		request.open('GET', '/monitorix-cgi/monitorix.cgi?mode=localhost&graph=gensens');
	else 
		request.open('GET', '/monitorix-cgi/monitorix.cgi?mode=localhost&graph=gensens&when=' + when);
	request.addEventListener('load', function(event) {
		if (this.status == 200) {
			var imgs = document.querySelectorAll('img');
			Object.entries(imgs).forEach(([i, img]) => {
				const token = img.src.replace(/^.*[\\\/]/, '').split('.');
				const newwhen = typeof when != 'undefined' ?  when : token[1];
				const newsrc = '/monitorix/imgs/' + token[0] + '.' + newwhen + '.png?ts=' + new Date().getTime();
				img.src = newsrc;
			});
		}
	});
	request.send();
}

function updateCurrent() {
	var request = new XMLHttpRequest();
	request.open('GET', 'weather.php');
	request.addEventListener('load', function(event) {
		if (this.status == 200) {
			var data = JSON.parse(this.responseText);
			Object.entries(data).forEach(([k, v]) => {
				const sensor = document.querySelector('#' + k);
				if (sensor) { 
					const value = sensor.querySelector('.value');
					if (value) value.innerHTML = v;
				}
			});
		}
	});
	request.send();
}

function show(e, when) {
	var whens = document.querySelectorAll('#navigation-top > ul > li');
	for (var i = 0; i < whens.length; i++) {
		whens[i].style.background = '#fff';
	}
	e.style.background = '#efefef';
	updateMonitorix(when);
}

function update() {
	updateMonitorix();
	updateCurrent();
}

window.onload = function() {
	updateMonitorix();
	updateCurrent();
	setInterval(update, 60000);
}
