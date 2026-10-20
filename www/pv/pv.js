// License: MIT - https://opensource.org/licenses/MIT
// Author: Michele Locati <michele@locati.it>
// Source: https://gist.github.com/mlocati/7210513
function perc2color(p) {
	if (p <= 0)
		return "LightGray";
	else if (p > 100) 
		return "MediumSpringGreen";
	
	var r, g, b = 0;

	if (p < 50) {
		r = 0xef;
		g = Math.round(5.1 * p);
	} else {
		g = 0xef;
		r = Math.round(510 - 5.10 * p);
	}

	const rgb = r * 0x10000 + g * 0x100 + b * 0x1;
	return '#' + ('000000' + rgb.toString(16)).slice(-6);
}

function hsl2rgb(h, s, l) {
	s /= 100;
	l /= 100;

	const k = n => (n + h / 30) % 12;
	const a = s * Math.min(l, 1 - l);
	const f = n => l - a * Math.max(-1, Math.min(k(n) - 3, Math.min(9 - k(n), 1)));

	const r = Math.round(255 * f(0));
	const g = Math.round(255 * f(8));
	const b = Math.round(255 * f(4));

	const rgb = r * 0x10000 + g * 0x100 + b * 0x1;
	return '#' + ('000000' + rgb.toString(16)).slice(-6);
}

function getPastelGradientColor(p) {
	if (p <= 0)
		return "LightGray";
	else if (p > 100) 
		return "MediumSpringGreen";

	// 0% ist Rot (0°), 50% ist Gelb (60°), 100% ist Grün (120°)
	const h = (p / 100) * 120;

	// Pastell-Eigenschaften: Niedrigere Sättigung (z.B. 85%) und hohe Helligkeit (z.B. 75%)
	const s = 85;
	const l = 65;
	
	const rgb = hsl2rgb(h, s, l);
	return rgb;
}

function getGradientColor(p) {
	var color = 0;
	
	if (p <= 0)
		color = "LightGray";
	else if (p > 100) 
		color = "MediumSpringGreen";
	else if (p > 90)
		color = "LawnGreen";
	else if (p > 80)
		color = "GreenYellow";
	else if (p > 70)
		color = "Gold";
	else if (p > 60)
		color = "Orange";
	else if (p > 50)
		color = "DarkOrange";
	else if (p > 40)
		color = "LightSalmon";
	else if (p > 30)
		color = "Coral";
	else if (p > 20)
		color = "Tomato";
	else if (p > 10)
		color = "OrangeRed";
	else 
		color = "Red";
	
	return color;
}


function imgClick() {
	document.location.href = this.src;
}

function toggle(e) {
	var id = e.currentTarget.getAttribute('data-id');
	if (id != 0) {
		var host = e.currentTarget.getAttribute('data-host');
		var r = e.currentTarget.getAttribute('data-r');
		var x = '/pv/pv.php?cmd=toggle&id=' + id + '&r=' + r;
		var request = new XMLHttpRequest();
		request.open("GET", x);
		request.send();
	}
}

function potd(e) {
	var p = document.querySelector('input[name=potd]:checked').value
	if (p != 0) {
		var request = new XMLHttpRequest();
		var x = '/pv/pv.php?cmd=potd&value=' + p;
		request.open("GET", x);
		request.send();
	}
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


function update_devices() {
	var request = new XMLHttpRequest();
	request.open("GET", "/pv/data/devices.json");
	request.addEventListener('load', function(event) {
		if (this.status == 200) {
			var data = JSON.parse(this.responseText);
			var dl = document.querySelector('dl');
			dl.innerHTML = '';
			var potd = data.potd.toLowerCase();
			document.getElementById(potd).checked = true;
			Object.entries(data.devices).forEach(([k, v]) => {
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
						var bgcolor = getGradientColor(v / 10);
						item.style.backgroundColor = bgcolor;
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
					if (k == 'rsl') {
						if (v == 0)
							item.style.backgroundColor = "lightgrey";
						else if (v < 90)
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

	var radios = document.forms["form-potd"].elements["potd"];
	for(var i = 0, max = radios.length; i < max; i++)
		radios[i].onclick = potd;

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
