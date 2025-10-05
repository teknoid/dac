function imgClick() {
	document.location.href = this.src;
}

function update_sensors() {
	var request = new XMLHttpRequest();
	request.open("GET", "/pv/data/sensors.json");
	request.addEventListener('load', function(event) {
		if (this.status == 200) {
			var data = JSON.parse(this.responseText);
			Object.entries(data).forEach(([k, v]) => {
				var item = document.querySelector('.dstate .' + k);
				if (item) { 
					var value = item.querySelector('.v');
					value.innerHTML = Number(v).toLocaleString('de-DE');
				}
			});
		}
	});
	request.send();
}

function toggle(e) {
	var host = e.currentTarget.getAttribute('data-host');
	var id = e.currentTarget.getAttribute('data-id');
	var r = e.currentTarget.getAttribute('data-r');
	var x = '/pv/pv.php?id=' + id + '&r=' + r;
	if (id != 0) {
		var request = new XMLHttpRequest();
		request.open("GET", x);
		request.send();
	}
}

function update_devices() {
	var request = new XMLHttpRequest();
	request.open("GET", "/pv/data/devices.json");
	request.addEventListener('load', function(event) {
		if (this.status == 200) {
			var data = JSON.parse(this.responseText);
			var dl = document.querySelector('dl');
			dl.innerHTML = '';
			Object.entries(data).forEach(([k, v]) => {
				var dd = document.createElement("dd");
				dd.classList.add(v.name);

				var height = v.load * 100 / v.total;
				var clazz;
				if (v.state == 0 || v.state == 1) {
					dd.classList.add('d');
					clazz = 'd';
				} else if (v.state == 2) {
					dd.classList.add('z');
					clazz = 'z';
				} else if (v.load < 0) {
					clazz = 'm';
					height *= -1;
				} else {
					clazz = 'p';
				}

				dd.setAttribute('data-host', v.host);
				dd.setAttribute('data-id', v.id);
				dd.setAttribute('data-r', v.r);
				dd.addEventListener("click", toggle);
				dd.innerHTML = '<span class="bar ' + clazz + '" style="height:' + height + '%;"></span><div class="load">' + v.load + '</div><div class="name">' + v.name + '</div>'
				dl.appendChild(dd);
			});
		}
	});
	request.send();
}

function update_state(file, selector) {
	var request = new XMLHttpRequest();
	request.open("GET", file);
	request.addEventListener('load', function(event) {
		if (this.status == 200) {
			var data = JSON.parse(this.responseText);
			Object.entries(data).forEach(([k, v]) => {
				var item = document.querySelector(selector + ' .' + k);
				if (item) {
					if (item.classList.contains('percent')) {
						if (v == 0)
							item.style.backgroundColor = "lightgrey";
						else if (v < 250)
							item.style.backgroundColor = "red";
						else if (v < 500)
							item.style.backgroundColor = "orangered";
						else if (v < 750)
							item.style.backgroundColor = "coral";
						else if (v < 900)
							item.style.backgroundColor = "orange";
						else if (v < 1000)
							item.style.backgroundColor = "greenyellow";
						else 
							item.style.backgroundColor = "palegreen";
					} else {
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
					}

					var n = Number(v);
					if (item.classList.contains('percent'))
						n = Number(v/10).toFixed(1);
					if (k == 'ttl')
						n = Number(v/60).toFixed(1);
					if (k == 'pload') {
						if (v < 100)
							item.style.backgroundColor = "coral";
						else if (v < 110)
							item.style.backgroundColor = "orange";
						else
							item.style.backgroundColor = "palegreen";
					}

					var value = item.querySelector('.v');
					value.innerHTML = Number(n).toLocaleString('de-DE');
				}
			});
		}
	});
	request.send();
}

function update_gstate() {
	update_state("/pv/data/gstate.json", ".gstate");
}

function update_pstate() {
	update_state("/pv/data/pstate.json", ".pstate");
}

function update_dstate() {
	update_state("/pv/data/dstate.json", ".dstate");
}

window.onload = function() {
	var images = document.getElementsByClassName("svg");
	for (var i=0; i<images.length; i++) 
		images[i].addEventListener("click", imgClick.bind(images[i]), false);

	update_pstate();
	update_dstate();
	update_devices();
	update_gstate();
	update_sensors();
	
	setInterval(update_pstate, 2000);
	setInterval(update_dstate, 2000);
	setInterval(update_devices, 2000);
	setInterval(update_gstate, 30000);
	setInterval(update_sensors, 30000);
}
