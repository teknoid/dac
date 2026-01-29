<?php

$c = $_GET['cmd'];
if (!isset($c)) {
	exit();
}

$v = $_GET['value'];
if (isset($v)) {
	$m = $v;
} else {
	$json = array();
	foreach($_GET as $key => $value) {
		$json[$key] = $value;
	}
	$m = json_encode($json);
}

$t = "solar/cmd/" . $c;
$cmd = "mosquitto_pub -h mqtt -t '" . $t . "'" . " -m '" . $m . "'";
// echo $cmd;
system($cmd); 

?>
