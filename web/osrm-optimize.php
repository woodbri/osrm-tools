<?php

  // Database connection settings
  define("DEBUG"  , "0");

  define("PG_DB"  , "osrm_test");
  define("PG_HOST", "localhost");
  define("PG_USER", "postgres");
  define("PG_PORT", "5432");
  define("TABLE",   "ddnoded2");

  define("OSRM_URL", "http://localhost:5000/viaroute?instructions=false&z=19&");

  $start  = $_REQUEST['start'];
  $stop   = $_REQUEST['stop'];
  $pnts   = $_REQUEST["pnts"];
  $mode   = $_REQUEST["mode"];
  $jsonp  = $_REQUEST["jsonp"];

  if (!function_exists('json_last_error_msg')) {
    function json_last_error_msg() {
        static $errors = array(
            JSON_ERROR_NONE             => null,
            JSON_ERROR_DEPTH            => 'Maximum stack depth exceeded',
            JSON_ERROR_STATE_MISMATCH   => 'Underflow or the modes mismatch',
            JSON_ERROR_CTRL_CHAR        => 'Unexpected control character found',
            JSON_ERROR_SYNTAX           => 'Syntax error, malformed JSON',
            JSON_ERROR_UTF8             => 'Malformed UTF-8 characters, possibly incorrectly encoded'
        );
        $error = json_last_error();
        return array_key_exists($error, $errors) ? $errors[$error] : "Unknown error ({$error})";
    }
  }

  function DBG($string) {
    if (DEBUG) {
      $stderr = fopen('php://stderr', 'w');
      fwrite($stderr, "osrm-optimize.php: " . $string . "\n");
      fclose($stderr);
    }
  }

  function respond($num, $arg, $res = '') {
    global $jsonp;
    $messages = Array(
    "Optimized path found.",
    "<ARG> routes failed while building the distance matrix.",
    "Error SQL (<ARG>) request failed."
    );

    $result = "{\"status\": " . $num .
              ", \"status_message\": \"" . $messages[$num] . "\"";
    $result = str_replace('<ARG>', $arg, $result);
    if (strlen($res) > 0) {
        $result .= ', ' . $res;
    }
    $result .= "}";

    if (isset($jsonp)) {
        $result = $jsonp . '(' . $result . ')';
        header('Content-Disposition: attachment; filename="route.js"');
        header('Content-type: text/javascript');
    }
    else {
        header('Content-type: text/json', true);
    }
    echo $result;
    exit;
  }

  DBG(print_r($_REQUEST, true));

  if (! isset($start))  $start = 1;
  if (! isset($stop))   $stop = -1;
  if (! isset($mode))   $mode = 0;

  if ($mode == 0)
    $metric = "total_time";
  else
    $metric = "total_distance";

  $istart = -1;
  $istop  = -1;

  $apnts = explode(';', $pnts);
  $ids = array();
  $p = array();
  for ($i=0; $i<count($apnts); $i++) {
    list($id, $x, $y) = explode(',', $apnts[$i]);
    DBG("id: $id, x: $x, y: $y");
    if ($id == $start) $istart = $i;
    if ($id == $stop)  $istop  = $i;
    array_push($ids, $id);
    array_push($p, "$y,$x");
  }

  # initialize distance matrix and distance matrix cache
  $dm = array();
  $dmc = array();
  $row = array();
  for ($i=0; $i<count($p); $i++) {
    array_push($row, 0.0);
  }
  for ($i=0; $i<count($p); $i++) {
    array_push($dm, $row);
    array_push($dmc, $row);
  }

  # call osrm and cache results
  $errors = array();
  for ($i=0; $i<count($p); $i++) {
    for ($j=$i; $j<count($p); $j++) {
      if ($i == $j) continue;
      $url = OSRM_URL . "loc=" . $p[$i] . "&loc=" . $p[$j];
      DBG("URL[$i,$j]: $url");
      $osrm = preg_replace('/[\x00-\x09\x0B\x0C\x0E-\x1F\x7F]/', '', file_get_contents($url));
      DBG("OSRM: $osrm");
      $dmc[$i][$j] = $json = json_decode($osrm, true);
      DBG("json_last_error: " . json_last_error_msg());
      DBG("route[$i,$j]");
      DBG("print_r: " . print_r($json, true));
      DBG("IF: " . (!isset($json['status']) || $json['status'] != 0));
      if (!isset($json['status']) || $json['status'] != 0) {
        array_push($errors, array($i, $j));
        $hint1 = '';
        $hint2 = '';
      }
      else {
        $hint1 = "&hint=" . $json['hint_data']['locations'][1];
        $hint2 = "&hint=" . $json['hint_data']['locations'][0];
        $dm[$i][$j] = $json['route_summary'][$metric];
      }
      $url = OSRM_URL . "loc=" . $p[$j] . $hint1 . "&loc=" . $p[$i] . $hint2;
      DBG("URL[$j,$i]: $url");
      $osrm = ereg_replace('/[\x00-\x09\x0B\x0C\x0E-\x1F\x7F]/', '', file_get_contents($url));
      DBG("OSRM: $osrm");
      $dmc[$j][$i] = $json = json_decode($osrm, true);
      DBG("json_last_error: " . json_last_error_msg());
      DBG("route[$j,$i]");
      DBG("print_r: " . print_r($json, true));
      if ($json['status'] != 0) {
        array_push($errors, array($j, $i));
      }
      else {
        $dm[$j][$i] = $json['route_summary'][$metric];
      }
    }
  }
  DBG("distanceMatrix=");
  DBG(print_r($dm, true));

  if (count($errors) > 0) {
    respond(1, count($errors));
  }

  // probably should check that the matrix is symmetric
  for ($i=0; $i<count($p); $i++) {
    for ($j=$i; $j<count($p); $j++) {
      if ($i == $j) continue;
      if ($dm[$i][$j] != $dm[$j][$i]) 
        $dm[$i][$j] = $dm[$j][$i] = ($dm[$i][$j] + $dm[$j][$i]) / 2.0;
    }
  }

  // create the distance matrix as a SQL string
  $sdm = '\'{';
  for ($i=0; $i<count($p); $i++) {
    if ($i == 0) $sdm .= '{';
    else $sdm .= '},{';
    $sdm .= implode(',', $dm[$i]);
  }
  $sdm .= '}}\'::float8[]';

  // Connect to database
  $con = pg_connect("dbname=".PG_DB." host=".PG_HOST." user=".PG_USER);

  $sql = "SELECT array_to_string(array_agg(id), ',')
            FROM pgr_tsp($sdm, $istart, $istop)";

  DBG("sql: " . $sql);

  $query = pg_query($con, $sql);

  if (! $query) {
    pg_close($con);
    respond(2, $sql);
  }

  $r = pg_fetch_result($query, 0, 0);
  DBG("order: $r");

  // Close database connection
  pg_close($con);

  $ordered = explode(',', $r);

  $result = "\"order\": [$r], \"route_geometry\": [\"";
  for ($i=0; $i<count($ordered)-1; $i++) {
    if ($i > 0) $result .= '","';
    $result .= $dmc[$ordered[$i]][$ordered[$i+1]]['route_geometry'];
  }

  // wrap the route back to the start is $istop == -1
  if ($istop == -1) {
    $result .= '","';
    $result .= $dmc[$ordered[count($ordered)-1]][$ordered[0]]['route_geometry'];
  }
  $result .= '"]';

  respond(0, '', $result);
?>
