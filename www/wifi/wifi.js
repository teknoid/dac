$(document).ready(function() {
    $('#wifi').DataTable( {
		searching: false,
		paging : false,
		ajax : 'wifi.php?file=ZOMBIE',
        language : { "url": "German.json" },
	});

	$('.reload').click(function reload(e) {
		var file = $(e.target).data('file');
		var dt = $('#wifi').DataTable();
		dt.ajax.url('wifi.php?file=' + file).load();
	});
});


