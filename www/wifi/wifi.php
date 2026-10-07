<?php

$f = $_GET['file'];
if (!isset($f)) {
	exit();
}

header('Content-Type: application/json; charset=utf-8');

$colums = array(1, 18, 32, 32, 8, 8, 8, 8, 10, 64);

print("{\"data\":[");

$file = fopen("/run/mcp/wifi-".$f.".txt", "r");
$lines = 0;
while (!feof($file)) {
	$line = fgets($file);
	
	// headline
	if (!$lines++)
		continue; 

	$x = 0; $y = $colums[0];
	$tag = trim(substr($line, $x, $y));
	
	$x = $x + $y + 1; $y = $colums[1];
	$mac = trim(substr($line, $x, $y));
	
	$x = $x + $y + 1; $y = $colums[2];
	$ssid = trim(substr($line, $x, $y));
	
	$x = $x + $y + 1; $y = $colums[3];
	$name = trim(substr($line, $x, $y));
	
	$x = $x + $y + 1; $y = $colums[4];
	$channel = trim(substr($line, $x, $y));
	
	$x = $x + $y + 1; $y = $colums[5];
	$signal = trim(substr($line, $x, $y));
	
	$x = $x + $y + 1; $y = $colums[6];
	$age = trim(substr($line, $x, $y));
	
	$x = $x + $y + 1; $y = $colums[7];
	$rate = trim(substr($line, $x, $y));
	
	$x = $x + $y + 1; $y = $colums[8];
	$count = trim(substr($line, $x, $y));
	
	$x = $x + $y + 1; $y = $colums[9];
	$hardware = trim(substr($line, $x, $y));

	$array = array($tag, $ssid, $channel, $signal, $age, $rate, $count, $name, $mac, $hardware);
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
