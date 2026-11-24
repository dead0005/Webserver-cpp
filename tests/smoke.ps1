param(
	[Parameter(Mandatory = $true)]
	[string]$ServerPath,

	[int]$Port = 0,

	[string]$CompilerBin = ""
)

$ErrorActionPreference = "Stop"
$serverPath = (Resolve-Path $ServerPath).Path
$projectRoot = Split-Path -Parent $PSScriptRoot
if (-not [string]::IsNullOrWhiteSpace($CompilerBin))
{
	$env:PATH = "$CompilerBin;$env:PATH"
}

if ($Port -eq 0)
{
	$portProbe = New-Object -TypeName System.Net.Sockets.TcpListener `
		-ArgumentList @([System.Net.IPAddress]::Loopback, 0)
	$portProbe.Start()
	$Port = $portProbe.LocalEndpoint.Port
	$portProbe.Stop()
}

$server = Start-Process -FilePath $serverPath `
	-ArgumentList @("127.0.0.1", "$Port") `
	-WorkingDirectory $projectRoot -PassThru -NoNewWindow

function Send-Request
{
	param(
		[string]$Request,
		[int]$SplitAfter = -1
	)

	$client = New-Object System.Net.Sockets.TcpClient
	try
	{
		$client.Connect("127.0.0.1", $Port)
		$stream = $client.GetStream()
		$stream.ReadTimeout = 5000
		$bytes = [System.Text.Encoding]::ASCII.GetBytes($Request)

		if ($SplitAfter -gt 0 -and $SplitAfter -lt $bytes.Length)
		{
			$stream.Write($bytes, 0, $SplitAfter)
			Start-Sleep -Milliseconds 100
			$stream.Write($bytes, $SplitAfter, $bytes.Length - $SplitAfter)
		}
		else
		{
			$stream.Write($bytes, 0, $bytes.Length)
		}

		$memory = New-Object System.IO.MemoryStream
		$buffer = New-Object byte[] 4096
		do
		{
			$count = $stream.Read($buffer, 0, $buffer.Length)
			if ($count -gt 0)
			{
				$memory.Write($buffer, 0, $count)
			}
		} while ($count -gt 0)

		return [System.Text.Encoding]::UTF8.GetString($memory.ToArray())
	}
	finally
	{
		$client.Dispose()
	}
}

function Assert-Response
{
	param(
		[string]$Response,
		[string]$Pattern,
		[string]$TestName
	)

	if ($Response -notmatch $Pattern)
	{
		throw "$TestName failed. Response:`n$Response"
	}
}

try
{
	$ready = $false
	for ($attempt = 0; $attempt -lt 30; $attempt++)
	{
		if ($server.HasExited)
		{
			throw "Server exited during startup with code $($server.ExitCode)."
		}

		try
		{
			$probe = New-Object System.Net.Sockets.TcpClient
			$probe.Connect("127.0.0.1", $Port)
			$probe.Dispose()
			$ready = $true
			break
		}
		catch
		{
			Start-Sleep -Milliseconds 200
		}
	}

	if (-not $ready)
	{
		throw "Server did not listen on port $Port."
	}

	$response = Send-Request "GET / HTTP/1.1`r`nHost: localhost`r`n`r`n"
	Assert-Response $response "200 OK" "Static GET"
	Assert-Response $response "Hello, world!" "Static file body"

	$response = Send-Request "GET /api/status HTTP/1.1`r`nHost: localhost`r`n`r`n"
	Assert-Response $response "200 OK" "Status API"
	Assert-Response $response '"status":"running"' "Status API body"

	$response = Send-Request "POST /api/echo HTTP/1.1`r`nHost: localhost`r`nContent-Length: 5`r`n`r`nhello"
	Assert-Response $response "200 OK" "POST echo"
	Assert-Response $response "hello" "POST echo body"

	$response = Send-Request "GET /not-found HTTP/1.1`r`nHost: localhost`r`n`r`n"
	Assert-Response $response "404 Not Found" "Missing route"

	$response = Send-Request "HEAD /not-found HTTP/1.1`r`nHost: localhost`r`n`r`n"
	Assert-Response $response "404 Not Found" "HEAD status"
	$bodyStart = $response.IndexOf("`r`n`r`n")
	if ($bodyStart -lt 0 -or $response.Substring($bodyStart + 4).Length -ne 0)
	{
		throw "HEAD response included a body."
	}

	$response = Send-Request "PUT / HTTP/1.1`r`nHost: localhost`r`n`r`n"
	Assert-Response $response "405 Method Not Allowed" "Unsupported method"
	Assert-Response $response "Allow: GET, HEAD, OPTIONS" "Allowed methods"

	$response = Send-Request "OPTIONS /api/echo HTTP/1.1`r`nHost: localhost`r`n`r`n"
	Assert-Response $response "204 No Content" "OPTIONS"
	Assert-Response $response "Allow: POST, OPTIONS" "Route-specific OPTIONS"

	$response = Send-Request "GET /%2e%2e/%2e%2e/windows/win.ini HTTP/1.1`r`nHost: localhost`r`n`r`n"
	Assert-Response $response "400 Bad Request|403 Forbidden" "Traversal rejection"

	$response = Send-Request "GET /api/status HTTP/1.1`r`n`r`n"
	Assert-Response $response "400 Bad Request" "Required HTTP/1.1 Host header"

	$response = Send-Request "POST /api/echo HTTP/1.1`r`nHost: localhost`r`nContent-Length: 1048577`r`n`r`n"
	Assert-Response $response "413 Content Too Large" "Request body limit"

	$response = Send-Request "POST /api/echo HTTP/1.1`r`nHost: localhost`r`nTransfer-Encoding: chunked`r`n`r`n"
	Assert-Response $response "501 Not Implemented" "Unsupported transfer encoding"

	$request = "GET /api/status HTTP/1.1`r`nHost: localhost`r`n`r`n"
	$response = Send-Request $request 35
	Assert-Response $response "200 OK" "Fragmented TCP request"
	Assert-Response $response '"status":"running"' "Fragmented request body"

	# --- PayCore Payment Engine Smoke Tests ---
	$payBody = '{"amount":25000,"currency":"INR","description":"Smoke test"}'
	$payReq = "POST /payments HTTP/1.1`r`nHost: localhost`r`nIdempotency-Key: smoke-001`r`nContent-Type: application/json`r`nContent-Length: $($payBody.Length)`r`n`r`n$payBody"
	$response = Send-Request $payReq
	Assert-Response $response "201 Created" "Payment Creation"
	Assert-Response $response '"status": "AUTHORIZED"' "Payment Auto-Auth Status"

	# Extract transaction ID
	if ($response -match '"id": "([^"]+)"')
	{
		$txnId = $matches[1]

		# Idempotent retry: same key, same payload -> 200 OK
		$retryResponse = Send-Request $payReq
		Assert-Response $retryResponse "200 OK" "Idempotent Retry"
		Assert-Response $retryResponse $txnId "Idempotent Matched ID"

		# Conflict: same key, different payload -> 409 Conflict
		$conflictBody = '{"amount":99999,"currency":"INR","description":"Conflict test"}'
		$conflictReq = "POST /payments HTTP/1.1`r`nHost: localhost`r`nIdempotency-Key: smoke-001`r`nContent-Type: application/json`r`nContent-Length: $($conflictBody.Length)`r`n`r`n$conflictBody"
		$conflictResponse = Send-Request $conflictReq
		Assert-Response $conflictResponse "409 Conflict" "Idempotency Conflict"

		# List payments -> 200 OK
		$listResponse = Send-Request "GET /payments HTTP/1.1`r`nHost: localhost`r`n`r`n"
		Assert-Response $listResponse "200 OK" "List Payments"
		Assert-Response $listResponse $txnId "List Contains ID"

		# Get payment by ID -> 200 OK
		$getResponse = Send-Request "GET /payments/$txnId HTTP/1.1`r`nHost: localhost`r`n`r`n"
		Assert-Response $getResponse "200 OK" "Get Payment By ID"
		Assert-Response $getResponse $txnId "Get Payment Payload"

		# Capture payment -> 200 OK, CAPTURED
		$captureResponse = Send-Request "POST /payments/$txnId/capture HTTP/1.1`r`nHost: localhost`r`n`r`n"
		Assert-Response $captureResponse "200 OK" "Capture Payment"
		Assert-Response $captureResponse '"status": "CAPTURED"' "Captured Status"

		# Refund payment -> 200 OK, REFUNDED
		$refundResponse = Send-Request "POST /payments/$txnId/refund HTTP/1.1`r`nHost: localhost`r`n`r`n"
		Assert-Response $refundResponse "200 OK" "Refund Payment"
		Assert-Response $refundResponse '"status": "REFUNDED"' "Refunded Status"

		# Cancel on REFUNDED -> 422 Unprocessable Entity
		$cancelResponse = Send-Request "POST /payments/$txnId/cancel HTTP/1.1`r`nHost: localhost`r`n`r`n"
		Assert-Response $cancelResponse "422 Unprocessable Entity" "Illegal State Transition"

		# --- Phase 2: Double-Entry Ledger Verification ---
		# GET /ledger -> should contain journal entries for capture and refund
		$ledgerResponse = Send-Request "GET /ledger HTTP/1.1`r`nHost: localhost`r`n`r`n"
		Assert-Response $ledgerResponse "200 OK" "Get Ledger Entries"
		Assert-Response $ledgerResponse "merchant:settlement" "Ledger has merchant account"
		Assert-Response $ledgerResponse "escrow:customer" "Ledger has escrow account"

		# GET /accounts -> should list accounts
		$accountsResponse = Send-Request "GET /accounts HTTP/1.1`r`nHost: localhost`r`n`r`n"
		Assert-Response $accountsResponse "200 OK" "List Accounts"
		Assert-Response $accountsResponse "merchant:settlement" "Accounts list has merchant account"

		# GET /accounts/merchant:settlement -> specific account detail
		$merchantResponse = Send-Request "GET /accounts/merchant:settlement HTTP/1.1`r`nHost: localhost`r`n`r`n"
		Assert-Response $merchantResponse "200 OK" "Get Merchant Account"
		Assert-Response $merchantResponse '"account_id": "merchant:settlement"' "Merchant Account ID"
	}
	else
	{
		throw "Failed to extract transaction ID from payment creation response."
	}

	Write-Output "All HTTP and PayCore smoke tests passed."
}
finally
{
	if (-not $server.HasExited)
	{
		Stop-Process -Id $server.Id -Force
	}
}
