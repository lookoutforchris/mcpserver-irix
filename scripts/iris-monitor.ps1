param([string]$Host = "localhost", [int]$Port = 8888)

$client = New-Object System.Net.Sockets.TcpClient
$client.Connect($Host, $Port)
$stream = $client.GetStream()

$stdin  = [System.Console]::OpenStandardInput()
$stdout = [System.Console]::OpenStandardOutput()

$toNet   = $stdin.CopyToAsync($stream)
$fromNet = $stream.CopyToAsync($stdout)

[void][System.Threading.Tasks.Task]::WhenAny($toNet, $fromNet).GetAwaiter().GetResult()
$client.Close()
