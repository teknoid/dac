$(document).ready(function() {
    var dt = $('#wifi').DataTable({
//		searching: false,
		paging : false,
		ajax : 'wifi.php?file=ZOMBIE',
        language : { "url": "German.json" },
        order: [],
//        columnDefs: [{ orderable: false, targets: 0 }]
	});

	// set focus to search input when datatable is ready
	dt.ready(function() {
		$('#dt-search-0').focus();
	});

	// reload on button click
	$('.reload').click(function(e) {
		var file = $(e.target).data('file');
		var dt = $('#wifi').DataTable();
		dt.ajax.url('wifi.php?file=' + file).load();
		$('#dt-search-0').focus();
	});

	// reload every 60 sec
	setInterval(function() {
		var dt = $('#wifi').DataTable();
		dt.ajax.reload(null, false);
	}, 60000);
});


