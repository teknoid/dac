<?php
header('Content-Type: application/json');

$mtime = preg_replace("/\r|\n/", "", file_get_contents('/ram/webcam/.mtime'));
$temp  = preg_replace("/\r|\n/", "", file_get_contents('/ram/mqtt/433/Prologue-TH/44/temperature_C'));
$humi  = preg_replace("/\r|\n/", "", file_get_contents('/ram/mqtt/433/Prologue-TH/44/humidity'));
#$temp  = preg_replace("/\r|\n/", "", file_get_contents('/ram/mqtt/433/Nexus-TH/6/temperature_C'));
#$humi  = preg_replace("/\r|\n/", "", file_get_contents('/ram/mqtt/433/Nexus-TH/6/humidity'));
$lumi  = preg_replace("/\r|\n/", "", file_get_contents('/ram/mqtt/picam/BH1750/lum_lux'));
$baro  = preg_replace("/\r|\n/", "", file_get_contents('/ram/mqtt/picam/BMP085/baro'));

#$baro = ($baro + 101325) / 100;

$json = array(
	'mtime' => $mtime,
	'temp'  => $temp . ' °C',
	'humi'  => $humi . ' %',
	'lumi'  => $lumi . ' lx',
	'baro'  => $baro . ' hPa'
);
echo json_encode($json);
?>
