# run_test.ps1
# PowerShell helper to build and run the client/server on Windows (requires gcc from msys2/mingw or WSL)

param()

$serverExe = "server.exe"
$clientExe = "client.exe"

Write-Host "Attempting to build with gcc..."
gcc -o server.exe dns-server.c -Wall -Wextra
gcc -o client.exe dns-client.c -Wall -Wextra

Write-Host "Starting server on port 5353 (non-admin)..."
$startInfo = New-Object System.Diagnostics.ProcessStartInfo
$startInfo.FileName = (Get-Command .\server.exe).Source
$startInfo.Arguments = "5353"
$startInfo.RedirectStandardOutput = $true
$startInfo.UseShellExecute = $false
$proc = [System.Diagnostics.Process]::Start($startInfo)
Start-Sleep -Milliseconds 500

Write-Host "Running client against 127.0.0.1:5353"
.\client.exe 127.0.0.1 5353

Write-Host "Stopping server"
$proc.Kill()

Write-Host "Done"
