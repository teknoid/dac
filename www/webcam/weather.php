<?php
header('Content-Type: application/json');

$mtime = preg_replace("/\r|\n/", "", file_get_contents('/ram/webcam/.mtime'));
$temp  = preg_replace("/\r|\n/", "", file_get_contents('/ram/mqtt/433/Nexus-TH/60/temperature_C'));
$humi  = preg_replace("/\r|\n/", "", file_get_contents('/ram/mqtt/433/Nexus-TH/60/humidity'));
$lumi  = preg_replace("/\r|\n/", "", file_get_contents('/ram/mqtt/sensor/BH1750/lum_percent'));
$baro  = preg_replace("/\r|\n/", "", file_get_contents('/ram/mqtt/sensor/BMP085/baro'));

#$baro = ($baro + 101325) / 100;

$json = array(
	'mtime' => $mtime,
	'temp'  => $temp . ' °C',
	'humi'  => $humi . ' %',
	'lumi'  => $lumi . ' %',
	'baro'  => $baro . ' hPa'
);
echo json_encode($json);
?>
