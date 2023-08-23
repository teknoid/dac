function render(emodel, title, labels, datasets) {
	var echart = document.createElement("canvas");
	emodel.appendChild(echart);
	new Chart(echart, {
		type: 'line',
		data: { labels:labels, datasets:datasets },
		options: {
		    responsive: false,
		    plugins: {
		    	legend: {
		    		position: 'top',
		    	},
		    	title: {
		    		display: true,
		    		text: title
		    	}
		    }
		}
	});
}

function prepare(model, emodel, ids) {
	var labels = new Array();
	var ds_temp = new Array();
	var ds_pres = new Array();
	
	Object.entries(ids).forEach(([id, data]) => {
		var d_temp = new Array();
		var d_pres = new Array();
		
		for (var i=0; i<data.time.length; i++) {
			var x = data.time[i];
			var y_temp = data.temperature_C ? data.temperature_C[i] : 0;
			var y_pres = data.pressure_BAR ? data.pressure_BAR[i] : 0;
			if (!labels.includes(x)) labels.push(x);
			d_temp.push( {x:x, y:y_temp} );
			d_pres.push( {x:x, y:y_pres} );
		}
		
		ds_temp.push( { data:d_temp, label:id, borderColor:'#' + id, fill: false } );
		ds_pres.push( { data:d_pres, label:id, borderColor:'#' + id, fill: false } );
	});
	
	labels.sort();
	render(emodel, model + ' - Temperature', labels, ds_temp);
	render(emodel, model + ' - Pressure', labels, ds_pres);
}

function load() {
	var request = new XMLHttpRequest();
	request.open('GET', 'sensors.php');
	request.addEventListener('load', function(event) {
		if (this.status == 200) {
			const echarts = document.querySelector('#content');
			var data = JSON.parse(this.responseText);
			
			Object.entries(data).forEach(([model, ids]) => {
				if (model != '0') {;
					var emodel = document.createElement("span");
					emodel.setAttribute("class", "model");
					echarts.appendChild(emodel);

					prepare(model, emodel, ids);
					
					var ebreak = document.createElement("br");
					echarts.appendChild(ebreak);
				}
			});
		}
	});
	request.send();
}

window.onload = function() {
	load();
}