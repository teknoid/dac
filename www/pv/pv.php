<?php

$id = $_GET['id'];
$r = $_GET['r'];

$json = array(
	'id' => $id,
	'r'  => $r,
	'cmd'  => 'toggle'
);

$t = "solar";
$m = json_encode($json);

$cmd = "mosquitto_pub -h mqtt -t '" . $t . "'" . " -m '" . $m . "'";

// echo $cmd;

system($cmd); 

?>
