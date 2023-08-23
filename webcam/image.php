<?php

    function low($url) {
        $percent = 0.5;
        list ($width, $height) = getimagesize($url);
        $new_width = $width * $percent;
        $new_height = $height * $percent;
        $image_p = imagecreatetruecolor($new_width, $new_height);
        $image = imagecreatefromjpeg($url);
        imagecopyresampled($image_p, $image, 0, 0, 0, 0, $new_width, $new_height, $width, $height);
        imagejpeg($image_p, null, 100);
    }
    
    function high($url) {
        $image = imagecreatefromjpeg($url);
        imagejpeg($image, null, 100);
    }
    
    $method = $_SERVER['REQUEST_METHOD'];
    if ($method != 'GET' && $method != 'POST') {
        return;
    }
    
    $url = $_GET['url'];
    if (strpos($url, "/") !== false || strpos($url, "..") !== false) {
        return;
    }
    if (!file_exists($url)) {
        return;
    }
    
    header('Content-Type: image/jpeg');
    
    $referer = $_SERVER["HTTP_REFERER"];
    if (strpos($referer, "/l/") !== false) {
        low($url);
    } else {
        high($url);
    }

?>
