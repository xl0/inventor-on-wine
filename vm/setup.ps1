# First-logon setup for the reference VM: OpenSSH server with our key, no sleep.
# Runs from the unattend ISO; log in C:\setup.log.
Start-Transcript C:\setup.log

Add-WindowsCapability -Online -Name OpenSSH.Server~~~~0.0.1.0
Set-Service sshd -StartupType Automatic
Start-Service sshd

$keys = 'C:\ProgramData\ssh\administrators_authorized_keys'
Copy-Item (Join-Path $PSScriptRoot 'authorized_keys') $keys
icacls $keys /inheritance:r /grant 'Administrators:F' /grant 'SYSTEM:F'
New-ItemProperty HKLM:\SOFTWARE\OpenSSH -Name DefaultShell -Force `
    -Value C:\Windows\System32\WindowsPowerShell\v1.0\powershell.exe
if (-not (Get-NetFirewallRule -Name sshd -ErrorAction SilentlyContinue)) {
    New-NetFirewallRule -Name sshd -DisplayName 'OpenSSH Server' -Profile Any `
        -Direction Inbound -Protocol TCP -LocalPort 22 -Action Allow
}

powercfg /change monitor-timeout-ac 0
powercfg /change standby-timeout-ac 0

Stop-Transcript
