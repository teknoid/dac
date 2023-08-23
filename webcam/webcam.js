function high() {
	window.location.href = "/webcam/h/webcam.html";
}

function low() {
	window.location.href = "/webcam/l/webcam.html";
}

function live(res) {
	var hours = document.querySelectorAll('#navigation-top > ul > li');
	for (var i = 0; i < hours.length; i++) {
		hours[i].style.background = '#fff';
	}
	document.getElementById('video').style.display = "none";
	document.getElementById('navigation-top').style.display = "block";
	document.getElementById("image").style.display = "inline";
	if (res) {
		window.location.href = res;
	} else {
		window.location.href = window.location.href;
	}
}

function picture(e, src) {
	document.getElementById('sensors').style.display = "none";
	document.getElementById('video').style.display = "none";
	var hours = document.querySelectorAll('#navigation-top > ul > li');
	for (var i = 0; i < hours.length; i++) {
		hours[i].style.background = '#fff';
	}
	e.style.background = '#efefef';
	var myImage = document.getElementById("image");
	myImage.style.display = "inline";
	myImage.src = src;

}

function video(vidURL) {
	document.getElementById('sensors').style.display = "none";
	document.getElementById('image').style.display = "none";
	document.getElementById('navigation-top').style.display = "none";
	var myVideo = document.getElementById('video');
	myVideo.style.display = "inline";
	myVideo.src = vidURL;
	myVideo.load();
	myVideo.play();
}

function loadVideos() {
	var request = new XMLHttpRequest();
	request.open("GET", "../videos.php");
	request.addEventListener('load', function(event) {
		if (this.status == 200) {
			var items = JSON.parse(this.responseText);
			for (var i = 0; i < items.length; i++) {
				var item = items[i];
				var li = document.createElement("li");
				li.appendChild(document.createTextNode(item.name));
				li.setAttribute('onclick', 'video("../videos/' + item.file + '")')
				document.getElementById("videos").appendChild(li);
			}
		}
	});
	request.send();
}
function updateData() {
	var request = new XMLHttpRequest();
	request.open("GET", "../weather.php");
	request.addEventListener('load', function(event) {
		if (this.status == 200) {
			var data = JSON.parse(this.responseText);
			Object.entries(data).forEach(([k, v]) => {
				var sensor = document.querySelector('#' + k);
				if (sensor) { 
					var value = sensor.querySelector('.value');
					if (value) value.innerHTML = v;
				}
			});
		}
	});
	request.send();
}

function updateImage() {
	var curr = document.getElementById("image");
	if (curr.src.indexOf("&ts=") > -1) {
		curr.src = curr.src.substring(0, curr.src.lastIndexOf("&ts=")) + "&ts=" + new Date().getTime();
		updateData();
	}
}

function update() {
	updateImage();
}

window.onload = function() {
	updateData();
	loadVideos();
	setInterval(update, 10000);
}
