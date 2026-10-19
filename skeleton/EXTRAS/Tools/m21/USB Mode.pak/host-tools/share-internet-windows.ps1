# Share this PC's Internet connection with the handheld over USB (NAT), keeping the device's 192.168.7.x network.
#   .\share-internet-windows.ps1 on  [-Uplink "WLAN"] [-UsbIf "Ethernet 5"]
#   .\share-internet-windows.ps1 off
#   .\share-internet-windows.ps1 status
# Or just double-click share-internet-windows.bat (asks for administrator rights).
#
# The device is 192.168.7.1 and already points its default route/DNS at 192.168.7.2 (this PC's USB adapter).
# Method 1: WinNAT (New-NetNat) for 192.168.7.0/24 - not available on every Windows edition ("Invalid class").
# Method 2 (automatic fallback): Windows Internet Connection Sharing, with the ICS private address moved from the
#   default 192.168.137.1 to 192.168.7.2 (registry ScopeAddress) so it matches the device's gateway.
param(
	[Parameter(Position = 0)][ValidateSet('on', 'off', 'status')][string]$Action = 'status',
	[string]$Uplink,
	[string]$UsbIf
)
$ErrorActionPreference = 'Stop'
$Prefix = '192.168.7.0/24'
$HostIp = '192.168.7.2'
$NatName = 'USBShare'
$IcsReg = 'HKLM:\SYSTEM\CurrentControlSet\Services\SharedAccess\Parameters'

# --- need administrator rights ---
$principal = New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
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

function Remove-WinNat {
	try { Get-NetNat -Name $NatName -ErrorAction Stop | Remove-NetNat -Confirm:$false } catch { }
}

# ---- Internet Connection Sharing through the HNetCfg COM API ----
function Get-IcsConnections {
	$mgr = New-Object -ComObject HNetCfg.HNetShare
	foreach ($c in $mgr.EnumEveryConnection) {
		[pscustomobject]@{
			Name = $mgr.NetConnectionProps.Invoke($c).Name
			Conn = $c
			Cfg  = $mgr.INetSharingConfigurationForINetConnection.Invoke($c)
		}
	}
}

function Disable-AllIcs {
	foreach ($c in @(Get-IcsConnections)) {
		if ($c.Cfg.SharingEnabled) { try { $c.Cfg.DisableSharing() } catch { } }
	}
}

function Enable-Ics($usb, $up) {
	Disable-AllIcs
	# move the ICS private network from 192.168.137.1 to 192.168.7.2 (takes effect when the service starts)
	Stop-Service SharedAccess -Force -ErrorAction SilentlyContinue
	foreach ($n in 'ScopeAddress', 'ScopeAddressBackup', 'StandaloneDhcpAddress') {
		Set-ItemProperty -Path $IcsReg -Name $n -Value $HostIp -Type String
	}
	$all = @(Get-IcsConnections)
	$pub = $all | Where-Object { $_.Name -eq $up.Name } | Select-Object -First 1
	$priv = $all | Where-Object { $_.Name -eq $usb.Name } | Select-Object -First 1
	if (-not $pub -or -not $priv) { throw "Could not find the connections '$($up.Name)' / '$($usb.Name)' in the sharing API." }
	$pub.Cfg.EnableSharing(0)    # ICSSHARINGTYPE_PUBLIC  = the connection with Internet
	$priv.Cfg.EnableSharing(1)   # ICSSHARINGTYPE_PRIVATE = the USB adapter
	Start-Sleep -Seconds 4
	$now = (Get-NetIPAddress -InterfaceIndex $usb.ifIndex -AddressFamily IPv4 -ErrorAction SilentlyContinue).IPAddress
	Write-Host "USB adapter address is now: $now (expected $HostIp)"
	if ($now -notcontains $HostIp) {
		Write-Host "warning: the adapter did not get $HostIp. Unplug/replug the device once, or run 'off' then 'on' again." -ForegroundColor Yellow
	}
}

function Disable-Ics($usb) {
	Disable-AllIcs
	foreach ($n in 'ScopeAddress', 'ScopeAddressBackup', 'StandaloneDhcpAddress') {
		Remove-ItemProperty -Path $IcsReg -Name $n -ErrorAction SilentlyContinue
	}
	Stop-Service SharedAccess -Force -ErrorAction SilentlyContinue
	if ($usb) {
		try { Set-NetIPInterface -InterfaceIndex $usb.ifIndex -AddressFamily IPv4 -Dhcp Enabled } catch { }
	}
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
		$done = $false
		try {
			foreach ($a in @($usb, $up)) { Set-NetIPInterface -InterfaceIndex $a.ifIndex -AddressFamily IPv4 -Forwarding Enabled }
			Remove-WinNat
			New-NetNat -Name $NatName -InternalIPInterfaceAddressPrefix $Prefix | Out-Null
			$done = $true
			Write-Host "method: WinNAT (New-NetNat)"
		} catch {
			Write-Host "WinNAT not usable here ($($_.Exception.Message)); falling back to Internet Connection Sharing..." -ForegroundColor Yellow
		}
		if (-not $done) {
			try {
				Enable-Ics $usb $up
				Write-Host "method: Internet Connection Sharing (private address $HostIp)"
			} catch {
				Write-Host "Internet Connection Sharing failed: $($_.Exception.Message)" -ForegroundColor Red
				Write-Host "Manual way: Control Panel > Network Connections > '$($up.Name)' > Properties > Sharing >" -ForegroundColor Yellow
				Write-Host "  tick 'Allow other network users to connect...' and choose '$($usb.Name)'. Windows then sets that adapter to 192.168.137.1;" -ForegroundColor Yellow
				Write-Host "  in that case run on the device:  ip addr add 192.168.137.2/24 dev usb0 ; ip route replace default via 192.168.137.1" -ForegroundColor Yellow
				exit 1
			}
		}
		Write-Host "on. On the device try:  ping -c3 8.8.8.8 ; ping -c3 www.baidu.com"
	}
	'off' {
		Remove-WinNat
		$usb = $null
		try { $usb = Find-UsbAdapter } catch { }
		Disable-Ics $usb
		Write-Host "off (sharing removed). If the USB adapter keeps a stale address, replug the device."
	}
	'status' {
		try { Get-NetNat -Name $NatName -ErrorAction Stop | Format-List Name, InternalIPInterfaceAddressPrefix, Active } catch { Write-Host "WinNAT: none/unavailable" }
		foreach ($c in @(Get-IcsConnections)) {
			if ($c.Cfg.SharingEnabled) { Write-Host "ICS: '$($c.Name)' sharing type $($c.Cfg.SharingConnectionType)" }
		}
		Get-NetIPAddress -AddressFamily IPv4 | Where-Object { $_.IPAddress -like '192.168.7.*' -or $_.IPAddress -like '192.168.137.*' } |
			Format-Table InterfaceAlias, IPAddress -AutoSize
	}
}
