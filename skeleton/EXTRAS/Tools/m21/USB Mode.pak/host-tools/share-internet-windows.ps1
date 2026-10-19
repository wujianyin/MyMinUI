# Share this PC's Internet connection with the handheld over USB (NAT), keeping the device's 192.168.7.x network.
#   .\share-internet-windows.ps1 on  [-Uplink "Wi-Fi"] [-UsbIf "Ethernet 5"]
#   .\share-internet-windows.ps1 off
#   .\share-internet-windows.ps1 status
# Or just double-click share-internet-windows.bat (asks for administrator rights).
#
# The device is 192.168.7.1 and already points its default route/DNS at 192.168.7.2 (this PC's USB adapter,
# which it gets from the device's DHCP server).  Windows' built-in "Internet Connection Sharing" is NOT used on
# purpose: it forces the adapter to 192.168.137.1 and runs its own DHCP, which conflicts with 192.168.7.x.
# Instead: enable IP forwarding on the USB adapter and the uplink, and create a WinNAT (New-NetNat) for 192.168.7.0/24.
param(
	[Parameter(Position = 0)][ValidateSet('on', 'off', 'status')][string]$Action = 'status',
	[string]$Uplink,
	[string]$UsbIf
)
$ErrorActionPreference = 'Stop'
$Prefix = '192.168.7.0/24'
$NatName = 'USBShare'

# --- need administrator rights ---
$principal = New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
$isAdmin = $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) {
	$argList = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "`"$PSCommandPath`"", $Action)
	if ($Uplink) { $argList += @('-Uplink', "`"$Uplink`"") }
	if ($UsbIf) { $argList += @('-UsbIf', "`"$UsbIf`"") }
	Start-Process powershell -Verb RunAs -ArgumentList $argList -Wait
	exit
}

function Find-UsbAdapter {
	if ($UsbIf) { return Get-NetAdapter -Name $UsbIf }
	$ip = Get-NetIPAddress -AddressFamily IPv4 -ErrorAction SilentlyContinue |
		Where-Object { $_.IPAddress -like '192.168.7.*' } | Select-Object -First 1
	if (-not $ip) {
		throw "No adapter with a 192.168.7.x address. Enable USB Network on the device (RNDIS or NCM) and wait for the PC to get 192.168.7.2 (if it shows 169.254.x.x the device's DHCP is not running)."
	}
	return Get-NetAdapter -InterfaceIndex $ip.InterfaceIndex
}

function Find-Uplink($usb) {
	if ($Uplink) { return Get-NetAdapter -Name $Uplink }
	$r = Get-NetRoute -DestinationPrefix '0.0.0.0/0' -ErrorAction SilentlyContinue |
		Where-Object { $_.InterfaceIndex -ne $usb.ifIndex } | Sort-Object RouteMetric | Select-Object -First 1
	if (-not $r) { throw "No Internet route found on this PC. Connect to the Internet first or pass -Uplink <adapter name>." }
	return Get-NetAdapter -InterfaceIndex $r.InterfaceIndex
}

switch ($Action) {
	'on' {
		$usb = Find-UsbAdapter
		$up = Find-Uplink $usb
		Write-Host "USB adapter : $($usb.Name) ($($usb.InterfaceDescription))"
		Write-Host "Uplink      : $($up.Name) ($($up.InterfaceDescription))"
		if ($up.InterfaceDescription -match 'VPN|TAP|TUN|WireGuard|Virtual') {
			Write-Host "note: the uplink looks like a VPN/virtual adapter; if there is no Internet on the device, pass -Uplink <real adapter>" -ForegroundColor Yellow
		}
		foreach ($a in @($usb, $up)) { Set-NetIPInterface -InterfaceIndex $a.ifIndex -AddressFamily IPv4 -Forwarding Enabled }
		Get-NetNat -Name $NatName -ErrorAction SilentlyContinue | Remove-NetNat -Confirm:$false
		try {
			New-NetNat -Name $NatName -InternalIPInterfaceAddressPrefix $Prefix | Out-Null
		} catch {
			Write-Host "New-NetNat failed: $($_.Exception.Message)" -ForegroundColor Red
			Write-Host "Another NAT (WSL, Docker, Hyper-V) may overlap, or the WinNAT service is stopped: 'Get-NetNat' / 'Start-Service winnat'." -ForegroundColor Yellow
			exit 1
		}
		Write-Host "on. On the device try:  ping -c3 8.8.8.8 ; ping -c3 www.baidu.com"
	}
	'off' {
		Get-NetNat -Name $NatName -ErrorAction SilentlyContinue | Remove-NetNat -Confirm:$false
		try {
			$usb = Find-UsbAdapter
			Set-NetIPInterface -InterfaceIndex $usb.ifIndex -AddressFamily IPv4 -Forwarding Disabled
		} catch { }
		Write-Host "off (NAT removed; forwarding on the uplink was left as is)"
	}
	'status' {
		Get-NetNat -Name $NatName -ErrorAction SilentlyContinue | Format-List Name, InternalIPInterfaceAddressPrefix, Active
		Get-NetIPInterface -AddressFamily IPv4 | Where-Object { $_.Forwarding -eq 'Enabled' } |
			Format-Table InterfaceAlias, InterfaceIndex, Forwarding -AutoSize
		Get-NetIPAddress -AddressFamily IPv4 | Where-Object { $_.IPAddress -like '192.168.7.*' } |
			Format-Table InterfaceAlias, IPAddress -AutoSize
	}
}
