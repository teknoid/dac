function update_sensors() {
	var request = new XMLHttpRequest();
	request.open("GET", "/pv/data/sensors.json");
	request.addEventListener('load', function(event) {
		if (this.status == 200) {
			var data = JSON.parse(this.responseText);
			Object.entries(data).forEach(([k, v]) => {
				var item = document.querySelector('.gstate .' + k);
				if (item) { 
					var value = item.querySelector('.v');
					value.innerHTML = Number(v).toLocaleString('de-DE');
				}
			});
		}
	});
	request.send();
}

function update_dstate() {
	var request = new XMLHttpRequest();
	request.open("GET", "/pv/data/dstate.json");
	request.addEventListener('load', function(event) {
		if (this.status == 200) {
			var data = JSON.parse(this.responseText);
			var dl = document.querySelector('dl');
			dl.innerHTML = '';
			Object.entries(data).forEach(([k, v]) => {
				var height = v.load * 100 / v.total;
				var load = v.load;
				var clazz = 'bar';
				if (v.state == 3) {
					clazz += ' z';
				} else if (v.load < 0) {
					clazz += ' m';
					height *= -1;
				} else {
					clazz += ' p';
				}
				dl.innerHTML += '<dt>' + v.name + '</dt>';
				dl.innerHTML += '<dd class="' + v.name + '"><span class="' + clazz + '" style="height:' + height + '%;"></span><div class="load">' + v.load + '</div></dd>';
			});
		}
	});
	request.send();
}

function update_gstate() {
	var request = new XMLHttpRequest();
	request.open("GET", "/pv/data/gstate.json");
	request.addEventListener('load', function(event) {
		if (this.status == 200) {
			var data = JSON.parse(this.responseText);
			Object.entries(data).forEach(([k, v]) => {
				var item = document.querySelector('.gstate .' + k);
				if (item) { 
					if (v < -10) {
						item.classList.remove('noise');
						item.classList.remove(k + '-p');
						item.classList.add(k + '-m');
					} else if (v > 10) {
						item.classList.remove('noise');
						item.classList.remove(k + '-m');
						item.classList.add(k + '-p');
					} else {
						item.classList.remove(k + '-p');
						item.classList.remove(k + '-m');
						item.classList.add('noise');
					}
					var n = Number(v);
					if (k == 'ttl')
						n = Number(v/60).toFixed(1);
					if (k == 'soc')
						n = Number(v/10).toFixed(1);
					if (k == 'surv' || k == 'heat' || k == 'succ')
						n = Number(v/100).toFixed(2);
					var value = item.querySelector('.v');
					value.innerHTML = Number(n).toLocaleString('de-DE');
				}
			});
		}
	});
	request.send();
}

function update_pstate() {
	var request = new XMLHttpRequest();
	request.open("GET", "/pv/data/pstate.json");
	request.addEventListener('load', function(event) {
		if (this.status == 200) {
			var data = JSON.parse(this.responseText);
			Object.entries(data).forEach(([k, v]) => {
				var item = document.querySelector('.pstate .' + k);
				if (item) { 
					if (v < -10) {
						item.classList.remove('noise');
						item.classList.remove(k + '-p');
						item.classList.add(k + '-m');
					} else if (v > 10) {
						item.classList.remove('noise');
						item.classList.remove(k + '-m');
						item.classList.add(k + '-p');
					} else {
						item.classList.remove(k + '-p');
						item.classList.remove(k + '-m');
						item.classList.add('noise');
					}
					var value = item.querySelector('.v');
					value.innerHTML = Number(v).toLocaleString('de-DE');
				}
			});
		}
	});
	request.send();
}

function imgClick() {
	document.location.href = this.src;
}

window.onload = function() {
	var images = document.getElementsByClassName("svg");
	for (var i=0; i<images.length; i++) 
		images[i].addEventListener("click", imgClick.bind(images[i]), false);

	update_pstate();
	update_dstate();
	update_gstate();
	update_sensors();
	
	setInterval(update_pstate, 2000);
	setInterval(update_dstate, 2000);
	setInterval(update_gstate, 2000);
	setInterval(update_sensors, 60000);
}
