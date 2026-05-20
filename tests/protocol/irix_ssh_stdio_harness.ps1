param(
    [string]$ConfigPath = ".mcp.json",
    [string]$ServerName = "irix-octane2",
    [string]$ProjectRoot = "/home/chris/src/mcpserver-irix",
    [string]$TestRootName = ".mcp-test",
    [int]$TimeoutMs = 5000,
    [switch]$IncludeBridgeRoughEdges
)

$ErrorActionPreference = "Stop"

$script:PassCount = 0
$script:FailCount = 0
$script:SkipCount = 0
$script:NextId = 1
$script:Process = $null

function Add-Result {
    param(
        [string]$Status,
        [string]$Name,
        [string]$Detail = ""
    )

    if ($Status -eq "PASS") {
        $script:PassCount++
        Write-Host ("PASS  {0}" -f $Name)
    } elseif ($Status -eq "SKIP") {
        $script:SkipCount++
        Write-Host ("SKIP  {0}" -f $Name)
    } else {
        $script:FailCount++
        Write-Host ("FAIL  {0}" -f $Name)
    }

    if ($Detail.Length -gt 0) {
        Write-Host ("      {0}" -f $Detail)
    }
}

function Read-Line-With-Timeout {
    param(
        [System.Diagnostics.Process]$Proc,
        [int]$WaitMs
    )

    $task = $Proc.StandardOutput.ReadLineAsync()
    if ($task.Wait($WaitMs)) {
        return $task.Result
    }
    return $null
}

function Start-McpProcess {
    param(
        [object]$Server
    )

    $psi = [System.Diagnostics.ProcessStartInfo]::new()
    $psi.FileName = [string]$Server.command
    foreach ($arg in $Server.args) {
        $psi.ArgumentList.Add([string]$arg)
    }
    $psi.RedirectStandardInput = $true
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $psi.UseShellExecute = $false

    return [System.Diagnostics.Process]::Start($psi)
}

function Stop-McpProcess {
    if ($null -ne $script:Process) {
        if (-not $script:Process.HasExited) {
            $script:Process.Kill()
        }
        $script:Process.Dispose()
        $script:Process = $null
    }
}

function Send-McpRequest {
    param(
        [string]$Method,
        [object]$Params = $null
    )

    $id = $script:NextId
    $script:NextId++

    $request = [ordered]@{
        jsonrpc = "2.0"
        id = $id
        method = $Method
    }
    if ($null -ne $Params) {
        $request.params = $Params
    }

    $json = $request | ConvertTo-Json -Compress -Depth 20
    $script:Process.StandardInput.WriteLine($json)

    $line = Read-Line-With-Timeout -Proc $script:Process -WaitMs $TimeoutMs
    if ($null -eq $line) {
        throw "timed out waiting for response to $Method"
    }

    $response = $line | ConvertFrom-Json
    if ($response.id -ne $id) {
        throw "response id mismatch for $Method; expected $id, got $($response.id)"
    }
    return $response
}

function Send-McpNotification {
    param(
        [string]$Method,
        [object]$Params = $null
    )

    $request = [ordered]@{
        jsonrpc = "2.0"
        method = $Method
    }
    if ($null -ne $Params) {
        $request.params = $Params
    }

    $json = $request | ConvertTo-Json -Compress -Depth 20
    $script:Process.StandardInput.WriteLine($json)
}

function Invoke-Tool {
    param(
        [string]$Name,
        [object]$Arguments = @{}
    )

    $params = @{
        name = $Name
        arguments = $Arguments
    }
    $response = Send-McpRequest -Method "tools/call" -Params $params
    if ($null -ne $response.error) {
        throw "tool $Name returned JSON-RPC error: $($response.error.message)"
    }

    $text = $response.result.content[0].text
    return ($text | ConvertFrom-Json)
}

function Test-Condition {
    param(
        [string]$Name,
        [scriptblock]$Body
    )

    try {
        $detail = & $Body
        Add-Result -Status "PASS" -Name $Name -Detail ([string]$detail)
    } catch {
        Add-Result -Status "FAIL" -Name $Name -Detail $_.Exception.Message
    }
}

function Initialize-Session {
    param(
        [object]$Server
    )

    Stop-McpProcess
    $script:Process = Start-McpProcess -Server $Server
    $script:NextId = 1

    $params = @{
        protocolVersion = "2024-11-05"
        capabilities = @{}
        clientInfo = @{
            name = "irix-ssh-stdio-harness"
            version = "1.0"
        }
    }

    return Send-McpRequest -Method "initialize" -Params $params
}

function Invoke-BridgeNotificationProbe {
    param(
        [object]$Server
    )

    try {
        Stop-McpProcess
        $script:Process = Start-McpProcess -Server $Server
        $script:NextId = 1

        $params = @{
            protocolVersion = "2024-11-05"
            capabilities = @{}
            clientInfo = @{
                name = "irix-bridge-probe"
                version = "1.0"
            }
        }

        [void](Send-McpRequest -Method "initialize" -Params $params)
        Send-McpNotification -Method "notifications/initialized" -Params @{}

        $response = Send-McpRequest -Method "tools/list" -Params @{}
        if ($null -eq $response.result.tools) {
            throw "tools/list after notification did not return a tools array"
        }

        Add-Result -Status "PASS" -Name "bridge handles notification before next request"
    } catch {
        Add-Result -Status "FAIL" -Name "bridge handles notification before next request" -Detail $_.Exception.Message
    } finally {
        Stop-McpProcess
    }
}

function Get-ServerConfig {
    $resolvedConfigPath = $ConfigPath

    if (-not (Test-Path -LiteralPath $resolvedConfigPath)) {
        $dir = (Get-Location).Path
        while ($true) {
            $candidate = Join-Path $dir $ConfigPath
            if (Test-Path -LiteralPath $candidate) {
                $resolvedConfigPath = $candidate
                break
            }

            $parent = Split-Path -Parent $dir
            if ($parent -eq $dir -or [string]::IsNullOrEmpty($parent)) {
                break
            }
            $dir = $parent
        }
    }

    if (-not (Test-Path -LiteralPath $resolvedConfigPath)) {
        throw "config file not found: $ConfigPath"
    }

    $config = Get-Content -LiteralPath $resolvedConfigPath -Raw | ConvertFrom-Json
    $server = $config.mcpServers.$ServerName
    if ($null -eq $server) {
        throw "MCP server '$ServerName' not found in $resolvedConfigPath"
    }
    if ($server.type -ne "stdio") {
        throw "MCP server '$ServerName' is type '$($server.type)', expected 'stdio'"
    }
    $script:ConfigPath = $resolvedConfigPath
    return $server
}

$server = Get-ServerConfig
$testRoot = "$ProjectRoot/$TestRootName"
$testFile = "$testRoot/protocol-fixture.txt"
$renamedFile = "$testRoot/protocol-fixture-renamed.txt"

Write-Host "IRIX MCP SSH stdio harness"
Write-Host ("Server: {0}" -f $ServerName)
Write-Host ("Command: {0} {1}" -f $server.command, (($server.args | ForEach-Object { [string]$_ }) -join " "))
Write-Host ("Project root: {0}" -f $ProjectRoot)
Write-Host ""

try {
    $init = Initialize-Session -Server $server

    Test-Condition "initialize returns irix-mcpserver identity" {
        if ($init.result.serverInfo.name -ne "irix-mcpserver") {
            throw "serverInfo.name was '$($init.result.serverInfo.name)'"
        }
        "version $($init.result.serverInfo.version)"
    }

    Test-Condition "tools/list advertises v1 tools" {
        $tools = (Send-McpRequest -Method "tools/list" -Params @{}).result.tools
        $names = @($tools | ForEach-Object { $_.name })
        $required = @(
            "ping",
            "path_exists",
            "stat_path",
            "list_directory",
            "read_text_file",
            "tail_text_file",
            "search_text",
            "read_text_around_pattern",
            "safe_json_preview",
            "run_inspect_command",
            "create_text_file",
            "replace_text_file",
            "make_directory",
            "delete_text_file",
            "rename_path"
        )
        foreach ($name in $required) {
            if ($names -notcontains $name) {
                throw "missing tool: $name"
            }
        }
        "$($names.Count) tools"
    }

    Test-Condition "ping tool responds" {
        $result = Invoke-Tool -Name "ping"
        if ($result.ok -ne $true) {
            throw "ping ok was not true"
        }
        "$($result.server) $($result.version), profile=$($result.profile)"
    }

    Test-Condition "list_directory sees project root" {
        $result = Invoke-Tool -Name "list_directory" -Arguments @{ path = $ProjectRoot }
        if ($result.allowed -ne $true -or $result.exists -ne $true) {
            throw "allowed=$($result.allowed), exists=$($result.exists)"
        }
        $names = @($result.entries | ForEach-Object { $_.name })
        foreach ($name in @("AGENTS.md", "README.md", "src", "docs", "tests")) {
            if ($names -notcontains $name) {
                throw "missing entry: $name"
            }
        }
        "$($names.Count) entries"
    }

    Test-Condition "stat_path reports README.md as file" {
        $result = Invoke-Tool -Name "stat_path" -Arguments @{ path = "$ProjectRoot/README.md" }
        if ($result.allowed -ne $true -or $result.exists -ne $true -or $result.kind -ne "file") {
            throw "allowed=$($result.allowed), exists=$($result.exists), kind=$($result.kind)"
        }
        "$($result.size_bytes) bytes"
    }

    Test-Condition "read_text_file reads AGENTS.md" {
        $result = Invoke-Tool -Name "read_text_file" -Arguments @{
            path = "$ProjectRoot/AGENTS.md"
            start_line = 1
            max_lines = 12
        }
        if ($result.allowed -ne $true -or $result.exists -ne $true) {
            throw "allowed=$($result.allowed), exists=$($result.exists)"
        }
        if ($result.content -notmatch "IRIX MCP Server") {
            throw "expected heading not found"
        }
        "$($result.lines_returned) lines"
    }

    Test-Condition "tail_text_file returns README.md tail" {
        $result = Invoke-Tool -Name "tail_text_file" -Arguments @{
            path = "$ProjectRoot/README.md"
            lines = 5
        }
        if ($result.allowed -ne $true -or $result.exists -ne $true) {
            throw "allowed=$($result.allowed), exists=$($result.exists)"
        }
        "$($result.lines_returned) lines from line $($result.start_line)"
    }

    Test-Condition "search_text finds mcpserver in docs" {
        $result = Invoke-Tool -Name "search_text" -Arguments @{
            root_path = "$ProjectRoot/docs"
            pattern = "mcpserver"
            include_globs = @("*.md")
            max_results = 10
            case_sensitive = $false
        }
        if ($result.allowed -ne $true) {
            throw "allowed=$($result.allowed)"
        }
        if (@($result.matches).Count -lt 1) {
            throw "no matches returned"
        }
        "$(@($result.matches).Count) matches"
    }

    Test-Condition "read_text_around_pattern finds ANSI C" {
        $result = Invoke-Tool -Name "read_text_around_pattern" -Arguments @{
            path = "$ProjectRoot/AGENTS.md"
            pattern = "ANSI C"
            context_before = 2
            context_after = 2
            match_index = 1
            case_sensitive = $true
        }
        if ($result.allowed -ne $true -or $result.found -ne $true) {
            throw "allowed=$($result.allowed), found=$($result.found)"
        }
        "match line $($result.match_line)"
    }

    Test-Condition "safe_json_preview reads .mcp.json" {
        $result = Invoke-Tool -Name "safe_json_preview" -Arguments @{
            path = "$ProjectRoot/.mcp.json"
            top_level_only = $true
            max_bytes = 12000
        }
        if ($result.allowed -ne $true -or $result.exists -ne $true) {
            throw "allowed=$($result.allowed), exists=$($result.exists)"
        }
        if ($result.error) {
            throw "error=$($result.error)"
        }
        "preview length $($result.content.Length)"
    }

    Test-Condition "denied path does not reveal /etc/passwd existence" {
        $result = Invoke-Tool -Name "path_exists" -Arguments @{ path = "/etc/passwd" }
        if ($result.allowed -ne $false) {
            throw "expected allowed=false, got $($result.allowed)"
        }
        if ($result.exists -eq $true) {
            throw "denied path leaked exists=true"
        }
        "exists=$($result.exists)"
    }

    Test-Condition "nonexistent path inside root reports cleanly" {
        $path = "$ProjectRoot/.definitely-not-present-mcp-harness"
        $result = Invoke-Tool -Name "path_exists" -Arguments @{ path = $path }
        if ($result.allowed -ne $true) {
            throw "expected allowed=true, got $($result.allowed)"
        }
        if ($result.exists -ne $false) {
            throw "expected exists=false, got $($result.exists)"
        }
        "exists=false"
    }

    Test-Condition "run_inspect_command pwd is constrained" {
        $result = Invoke-Tool -Name "run_inspect_command" -Arguments @{
            command = "pwd"
            args = @()
        }
        if ($result.error) {
            throw "error=$($result.error)"
        }
        if ($result.exit_code -ne 0) {
            throw "exit_code=$($result.exit_code)"
        }
        $result.stdout.Trim()
    }

    Test-Condition "run_inspect_command rejects shell metacharacters" {
        $result = Invoke-Tool -Name "run_inspect_command" -Arguments @{
            command = "ls"
            args = @("$ProjectRoot;id")
        }
        if ($result.allowed -ne $false -and -not $result.error) {
            throw "expected rejection, got allowed=$($result.allowed)"
        }
        "rejected"
    }

    Test-Condition "run_inspect_command rejects unknown command" {
        $result = Invoke-Tool -Name "run_inspect_command" -Arguments @{
            command = "sh"
            args = @("-c", "id")
        }
        if ($result.allowed -ne $false) {
            throw "expected allowed=false, got $($result.allowed)"
        }
        "rejected sh"
    }

    Test-Condition "run_inspect_command echoes args in response" {
        $result = Invoke-Tool -Name "run_inspect_command" -Arguments @{
            command = "uname"
            args = @("-a")
        }
        if ($result.error) { throw "error=$($result.error)" }
        if ($result.exit_code -ne 0) { throw "exit_code=$($result.exit_code)" }
        if ($result.args -notcontains "-a") {
            throw "args not echoed: got $($result.args)"
        }
        $result.stdout.Trim()
    }

    Test-Condition "run_inspect_command cat reads file in root" {
        $result = Invoke-Tool -Name "run_inspect_command" -Arguments @{
            command = "cat"
            args = @("$ProjectRoot/README.md")
        }
        if ($result.error) { throw "error=$($result.error)" }
        if ($result.exit_code -ne 0) { throw "exit_code=$($result.exit_code)" }
        "$($result.stdout.Length) chars"
    }

    Test-Condition "run_inspect_command grep finds pattern in file" {
        $result = Invoke-Tool -Name "run_inspect_command" -Arguments @{
            command = "grep"
            args = @("-n", "ANSI", "$ProjectRoot/AGENTS.md")
        }
        if ($result.error) { throw "error=$($result.error)" }
        if ($result.exit_code -ne 0) { throw "exit_code=$($result.exit_code)" }
        "$(@($result.stdout -split '\n' | Where-Object { $_ -ne '' }).Count) match(es)"
    }

    Test-Condition "run_inspect_command diff compares two files" {
        $result = Invoke-Tool -Name "run_inspect_command" -Arguments @{
            command = "diff"
            args = @("-q", "$ProjectRoot/README.md", "$ProjectRoot/AGENTS.md")
        }
        if ($result.allowed -ne $true) { throw "allowed=$($result.allowed)" }
        if ($result.error -and $result.exit_code -ne 1) {
            throw "unexpected error=$($result.error)"
        }
        "exit_code=$($result.exit_code) (1=files differ, expected)"
    }

    Test-Condition "run_inspect_command nm inspects binary symbols" {
        $result = Invoke-Tool -Name "run_inspect_command" -Arguments @{
            command = "nm"
            args = @("-u", "$ProjectRoot/mcpserverd")
        }
        if ($result.error) { throw "error=$($result.error)" }
        if ($result.stdout -notmatch "execv") {
            throw "expected execv in symbol table"
        }
        "$(@($result.stdout -split '\n' | Where-Object { $_ -ne '' }).Count) symbols"
    }

    Test-Condition "run_inspect_command file identifies ELF binary" {
        $result = Invoke-Tool -Name "run_inspect_command" -Arguments @{
            command = "file"
            args = @("$ProjectRoot/mcpserverd")
        }
        if ($result.error) { throw "error=$($result.error)" }
        if ($result.stdout -notmatch "ELF") {
            throw "expected ELF in output: $($result.stdout)"
        }
        $result.stdout.Trim()
    }

    Test-Condition "run_inspect_command ps lists running processes" {
        $result = Invoke-Tool -Name "run_inspect_command" -Arguments @{
            command = "ps"
            args = @("-e")
        }
        if ($result.error) { throw "error=$($result.error)" }
        if ($result.exit_code -ne 0) { throw "exit_code=$($result.exit_code)" }
        "$(@($result.stdout -split '\n' | Where-Object { $_ -ne '' }).Count) processes"
    }

    Test-Condition "run_inspect_command df shows filesystem usage" {
        $result = Invoke-Tool -Name "run_inspect_command" -Arguments @{
            command = "df"
            args = @("-k")
        }
        if ($result.error) { throw "error=$($result.error)" }
        if ($result.exit_code -ne 0) { throw "exit_code=$($result.exit_code)" }
        ($result.stdout -split '\n' | Where-Object { $_ -ne '' } | Select-Object -Last 1).Trim()
    }

    Test-Condition "run_inspect_command denies path outside allowed roots" {
        $result = Invoke-Tool -Name "run_inspect_command" -Arguments @{
            command = "cat"
            args = @("/etc/passwd")
        }
        if ($result.allowed -ne $false) {
            throw "expected allowed=false, got $($result.allowed)"
        }
        "denied /etc/passwd"
    }

    Test-Condition "path traversal escape attempt is denied" {
        $result = Invoke-Tool -Name "path_exists" -Arguments @{
            path = "$ProjectRoot/../../etc/passwd"
        }
        if ($result.allowed -ne $false) {
            throw "expected allowed=false, got $($result.allowed)"
        }
        "denied"
    }

    Test-Condition "path with redundant traversal stays in root" {
        $leaf = $ProjectRoot.Split("/")[-1]
        $result = Invoke-Tool -Name "path_exists" -Arguments @{
            path = "$ProjectRoot/../$leaf/README.md"
        }
        if ($result.allowed -ne $true) {
            throw "expected allowed=true after canonicalization, got $($result.allowed)"
        }
        "allowed: canonicalized correctly"
    }

    $mkdirResult = Invoke-Tool -Name "make_directory" -Arguments @{ path = $testRoot }
    $mkdirOk = $mkdirResult.allowed -eq $true -and (
        $mkdirResult.created -eq $true -or $mkdirResult.error -eq "already exists"
    )
    if ($mkdirOk) {
        $mkdirDetail = if ($mkdirResult.created) { $testRoot } else { "$testRoot (already existed)" }
        Add-Result -Status "PASS" -Name "make_directory creates disposable test root" -Detail $mkdirDetail

        Test-Condition "create_text_file creates fixture" {
            $result = Invoke-Tool -Name "create_text_file" -Arguments @{
                path = $testFile
                content = "alpha`nbeta MCP-HARNESS-NEEDLE`ngamma`n"
            }
            if ($result.allowed -ne $true -or $result.created -ne $true) {
                throw "allowed=$($result.allowed), created=$($result.created), error=$($result.error)"
            }
            "created"
        }

        Test-Condition "replace_text_file updates fixture" {
            $result = Invoke-Tool -Name "replace_text_file" -Arguments @{
                path = $testFile
                content = "one`ntwo MCP-HARNESS-UPDATED`nthree`n"
            }
            if ($result.allowed -ne $true -or $result.replaced -ne $true) {
                throw "allowed=$($result.allowed), replaced=$($result.replaced), error=$($result.error)"
            }
            "replaced"
        }

        Test-Condition "read_text_file sees updated fixture" {
            $result = Invoke-Tool -Name "read_text_file" -Arguments @{
                path = $testFile
                start_line = 1
                max_lines = 10
            }
            if ($result.content -notmatch "MCP-HARNESS-UPDATED") {
                throw "updated content not found"
            }
            "$($result.lines_returned) lines"
        }

        Test-Condition "rename_path renames fixture" {
            $result = Invoke-Tool -Name "rename_path" -Arguments @{
                source_path = $testFile
                dest_path = $renamedFile
            }
            if ($result.allowed -ne $true -or $result.renamed -ne $true) {
                throw "allowed=$($result.allowed), renamed=$($result.renamed), error=$($result.error)"
            }
            "renamed"
        }

        Test-Condition "delete_text_file removes renamed fixture" {
            $result = Invoke-Tool -Name "delete_text_file" -Arguments @{ path = $renamedFile }
            if ($result.allowed -ne $true -or $result.deleted -ne $true) {
                throw "allowed=$($result.allowed), deleted=$($result.deleted), error=$($result.error)"
            }
            "deleted"
        }

        Test-Condition "write denied for .pem extension (global deny)" {
            $result = Invoke-Tool -Name "create_text_file" -Arguments @{
                path    = "$testRoot/secret.pem"
                content = "fake key material"
            }
            if ($result.allowed -ne $false) {
                throw "expected denied, got allowed=$($result.allowed)"
            }
            "denied .pem"
        }

        Test-Condition "write denied for .o extension (not in allowlist)" {
            $result = Invoke-Tool -Name "create_text_file" -Arguments @{
                path    = "$testRoot/object.o"
                content = "fake object file"
            }
            if ($result.allowed -ne $false) {
                throw "expected denied, got allowed=$($result.allowed)"
            }
            "denied .o"
        }

        Test-Condition "write denied for extensionless name" {
            $result = Invoke-Tool -Name "create_text_file" -Arguments @{
                path    = "$testRoot/my_binary"
                content = "fake executable"
            }
            if ($result.allowed -ne $false) {
                throw "expected denied, got allowed=$($result.allowed)"
            }
            "denied no extension"
        }

        Test-Condition "empty file create and read" {
            $createResult = Invoke-Tool -Name "create_text_file" -Arguments @{
                path = "$testRoot/empty.txt"; content = ""
            }
            if ($createResult.allowed -ne $true -or $createResult.created -ne $true) {
                throw "create: allowed=$($createResult.allowed), created=$($createResult.created)"
            }
            $readResult = Invoke-Tool -Name "read_text_file" -Arguments @{
                path = "$testRoot/empty.txt"; start_line = 1; max_lines = 5
            }
            Invoke-Tool -Name "delete_text_file" -Arguments @{ path = "$testRoot/empty.txt" } | Out-Null
            "lines=$($readResult.lines_returned)"
        }

        Test-Condition "file without final newline reads correctly" {
            $createResult = Invoke-Tool -Name "create_text_file" -Arguments @{
                path = "$testRoot/nonewline.txt"; content = "no newline here"
            }
            if ($createResult.allowed -ne $true -or $createResult.created -ne $true) {
                throw "create: allowed=$($createResult.allowed)"
            }
            $readResult = Invoke-Tool -Name "read_text_file" -Arguments @{
                path = "$testRoot/nonewline.txt"; start_line = 1; max_lines = 5
            }
            Invoke-Tool -Name "delete_text_file" -Arguments @{ path = "$testRoot/nonewline.txt" } | Out-Null
            "content='$($readResult.content.Trim())'"
        }

        Test-Condition "run_inspect_command grep searches test fixture" {
            $createResult = Invoke-Tool -Name "create_text_file" -Arguments @{
                path    = "$testRoot/greptest.txt"
                content = "line one`nMCP-GREP-TARGET found here`nline three`n"
            }
            if ($createResult.allowed -ne $true -or $createResult.created -ne $true) {
                throw "create fixture: allowed=$($createResult.allowed)"
            }
            $result = Invoke-Tool -Name "run_inspect_command" -Arguments @{
                command = "grep"
                args    = @("-n", "MCP-GREP-TARGET", "$testRoot/greptest.txt")
            }
            Invoke-Tool -Name "delete_text_file" -Arguments @{ path = "$testRoot/greptest.txt" } | Out-Null
            if ($result.error) { throw "error=$($result.error)" }
            if ($result.stdout -notmatch "MCP-GREP-TARGET") {
                throw "pattern not found in output"
            }
            $result.stdout.Trim()
        }
    } else {
        Add-Result -Status "SKIP" -Name "write tool sequence" -Detail "make_directory denied or unavailable; root is likely read-only"
    }

    Test-Condition "invalid method returns JSON-RPC error" {
        $response = Send-McpRequest -Method "definitely/not_a_method" -Params @{}
        if ($null -eq $response.error) {
            throw "missing JSON-RPC error"
        }
        "code $($response.error.code)"
    }

    Stop-McpProcess

    if ($IncludeBridgeRoughEdges) {
        Invoke-BridgeNotificationProbe -Server $server
    }
} finally {
    Stop-McpProcess
}

Write-Host ""
Write-Host ("Summary: {0} passed, {1} failed, {2} skipped" -f $script:PassCount, $script:FailCount, $script:SkipCount)

if ($script:FailCount -gt 0) {
    exit 1
}
