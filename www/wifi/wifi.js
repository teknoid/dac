$(document).ready(function() {
    $('#wifi').DataTable( {
		searching: false,
		paging : false,
		ajax : 'wifi.php?file=ZOMBIE',
        language : { "url": "German.json" },
//        columnDefs: [{ orderable: false, targets: 0 }]
	});

	// reload on button click
	$('.reload').click(function(e) {
		var file = $(e.target).data('file');
		var dt = $('#wifi').DataTable();
		dt.ajax.url('wifi.php?file=' + file).load();
	});
	
	// reload every 60 sec
	setInterval(function() {
		var dt = $('#wifi').DataTable();
		dt.ajax.reload(null, false);
	}, 60000);
});


