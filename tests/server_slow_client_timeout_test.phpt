--TEST--
Kislay Discovery's registry server enforces a read deadline on each connection, so a client that connects and sends nothing does not park a thread+fd forever
--EXTENSIONS--
kislayphp_discovery
--SKIPIF--
<?php
if (!extension_loaded('kislayphp_discovery')) {
    echo 'skip kislayphp_discovery not loaded';
}
?>
--FILE--
<?php
require __DIR__ . '/server_helper.inc';

$port = reserve_free_port();
$bootstrap = <<<PHP
\$registry = new Kislay\\Discovery\\ServiceRegistry();
\$registry->listen('127.0.0.1', {$port});
\$registry->run();
PHP;

// A short deadline so the test doesn't have to wait out the 15s production
// default - see kislayphp_discovery_server_run()'s accept loop for what
// consumes this.
$server = start_kislay_discovery_server($bootstrap, '127.0.0.1', $port, [
    'KISLAY_DISCOVERY_SERVER_IO_TIMEOUT_MS' => '300',
]);

try {
    // Connect but never send a byte - a minimal slow-loris client. Before
    // the fix this would park the server's handler thread (and this fd) in
    // a blocking recv() forever; the connection would just sit open with no
    // response ever arriving.
    $slow = @fsockopen('127.0.0.1', $port, $errno, $errstr, 2.0);
    if (!$slow) {
        fail("Unable to open slow-loris connection: {$errstr}");
    }
    stream_set_timeout($slow, 2);

    $start = microtime(true);
    $response = fread($slow, 4096);
    $elapsed_ms = (microtime(true) - $start) * 1000;
    fclose($slow);

    echo "server responded instead of hanging: " . (str_contains($response, '400') ? 'yes' : 'no') . "\n";
    // Generous upper bound (well under the 2s stream timeout, comfortably
    // above the 300ms deadline) - this must not be anywhere near "forever".
    echo "responded within a bounded window: " . ($elapsed_ms < 1500 ? 'yes' : 'no') . "\n";

    // A normal request on a fresh connection still works fine.
    $normal = @fsockopen('127.0.0.1', $port, $errno, $errstr, 2.0);
    if (!$normal) {
        fail("Unable to open normal connection: {$errstr}");
    }
    fwrite($normal, "GET /health HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n");
    stream_set_timeout($normal, 2);
    $normalResponse = stream_get_contents($normal);
    fclose($normal);
    echo "normal request still works: " . (str_contains($normalResponse, 'HTTP/1.1 200') ? 'yes' : 'no') . "\n";
} finally {
    stop_kislay_discovery_server($server);
}
?>
--EXPECT--
server responded instead of hanging: yes
responded within a bounded window: yes
normal request still works: yes
