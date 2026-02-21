--TEST--
Kislay Discovery uses optional richer methods on custom clients
--EXTENSIONS--
kislayphp_discovery
--FILE--
<?php
class OptionalDiscoveryClient implements Kislay\Discovery\ClientInterface {
    public array $calls = [];

    public function register(string $name, string $url): bool {
        $this->calls[] = 'register:' . $name . ':' . $url;
        return true;
    }

    public function deregister(string $name): bool {
        $this->calls[] = 'deregister:' . $name;
        return true;
    }

    public function resolve(string $name): ?string {
        $this->calls[] = 'resolve:' . $name;
        return 'http://127.0.0.1:9001';
    }

    public function list(): array {
        $this->calls[] = 'list';
        return ['svc' => 'http://127.0.0.1:9001'];
    }

    public function registerInstance(string $name, string $url, array $metadata = [], ?string $instanceId = null): bool {
        $this->calls[] = 'registerInstance:' . $name . ':' . $url . ':' . ($instanceId ?? '') . ':' . ($metadata['zone'] ?? '');
        return true;
    }

    public function deregisterInstance(string $name, ?string $instanceId = null): bool {
        $this->calls[] = 'deregisterInstance:' . $name . ':' . ($instanceId ?? '');
        return true;
    }

    public function listInstances(string $name): array {
        $this->calls[] = 'listInstances:' . $name;
        return [
            [
                'service' => $name,
                'instanceId' => 'inst-1',
                'url' => 'http://127.0.0.1:9001',
                'status' => 'UP',
                'lastHeartbeat' => 123,
                'metadata' => ['zone' => 'z1'],
            ],
        ];
    }

    public function heartbeat(string $name, ?string $instanceId = null): bool {
        $this->calls[] = 'heartbeat:' . $name . ':' . ($instanceId ?? '');
        return true;
    }

    public function setStatus(string $name, string $status, ?string $instanceId = null): bool {
        $this->calls[] = 'setStatus:' . $name . ':' . $status . ':' . ($instanceId ?? '');
        return true;
    }
}

$registry = new Kislay\Discovery\ServiceRegistry();
$client = new OptionalDiscoveryClient();
var_dump($registry->setClient($client));

var_dump($registry->register('svc', 'http://127.0.0.1:9001', ['zone' => 'z1'], 'inst-1'));
$instances = $registry->listInstances('svc');
var_dump(count($instances));
var_dump($instances[0]['instanceId']);

var_dump($registry->heartbeat('svc', 'inst-1'));
var_dump($registry->setStatus('svc', 'DOWN', 'inst-1'));
var_dump($registry->deregister('svc', 'inst-1'));

foreach ($client->calls as $call) {
    echo $call, "\n";
}
?>
--EXPECT--
bool(true)
bool(true)
int(1)
string(6) "inst-1"
bool(true)
bool(true)
bool(true)
registerInstance:svc:http://127.0.0.1:9001:inst-1:z1
listInstances:svc
heartbeat:svc:inst-1
setStatus:svc:DOWN:inst-1
resolve:svc
deregisterInstance:svc:inst-1
