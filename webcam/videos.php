<?php
header('Content-Type: application/json');

// read videos directory and build json file descriptions according to following scheme:
// gartencam-20180823-drecksau.mp4 => "23.08.2018 Drecksau"

$videos = "/home/www/webcam/videos/";
$files = array();
if ($handle = opendir($videos)) {
    while (false !== ($file = readdir($handle))) {
        if ($file != "." && $file != "..") {
            $files[filemtime($videos . $file)] = $file;
        }
    }
    closedir($handle);
}

// sort by date
ksort($files);

// build json
$json = array();
foreach ($files as $mtime => $file) {
    $tokens = explode("-", basename($file, ".mp4"));
    $tokens = array_map(function ($word) { return ucwords($word); }, $tokens);
    unset($tokens[0]);
    unset($tokens[1]);
    $name = date('d.m.Y', $mtime) . " " . implode(" ", $tokens);
    $entry = array(
        'name' => trim($name),
        'file' => $file
    );
    array_push($json, $entry);
}

echo json_encode($json);
?>
