<?php

$f = $_GET['file'];
if (!isset($f)) {
	exit();
}

header('Content-Type: application/json; charset=utf-8');

print("{\"data\":[");

$file = fopen("/run/mcp/wifi-".$f.".txt", "r");
$lines = 0;
while (!feof($file)) {
	$line = fgets($file);
	
	// headline
	if (!$lines++)
		continue; 

	$tag = trim(substr($line, 0, 1));
	$mac = trim(substr($line, 2, 19));
	$ssid = trim(substr($line, 21, 32));
	$name = trim(substr($line, 54, 32));
	$channel = trim(substr($line, 87, 8));
	$signal = trim(substr($line, 96, 8));
	$age = trim(substr($line, 105, 8));
	$rate = trim(substr($line, 114, 8));
	$count = trim(substr($line, 123, 10));
	$hardware = trim(substr($line, 134, 64));

	$array = array($tag, $mac, $ssid, $name, $channel, $signal, $age, $rate, $count, $hardware);
	$json = json_encode($array);

	// empty line
	if ($mac) {
		if ($lines > 2)
			print(",");
		print($json);
	}
}
fclose($file);

print("]}");

?>
