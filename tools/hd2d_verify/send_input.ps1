param(
  [int]$TargetPid,
  [string]$Chars = "",
  [int]$VK = 0,
  [int]$Scan = 0,
  [int]$DelayMs = 150
)
# HengbandHd2d.exe (voxel HD2D) into which to inject input, chosen by PID.
# For repeatable HD2D keyboard tests, prefer --test-input-file and send_test_key.py.
# They use SDL events directly; this legacy script depends on Win32 message translation.
# Two windows can coexist on this desktop (other threads run their own copy),
# so never pick the window by title or by "first process" -- always by PID.
#
#   -Chars "nncde"        printable keys; posted as WM_CHAR -> SDL_TEXTINPUT.
#                         This is the only path the in-game side listens to.
#   -VK 0x20 -Scan 0x39   one special key; posted as WM_KEYDOWN/UP with the
#                         scancode in lParam (SDL reads the scancode, not VK).
#                         The core-select menu needs this (j/k/SPACE/RETURN).
#                         Common pairs: SPACE 0x20/0x39, RETURN 0x0D/0x1C,
#                         ESC 0x1B/0x01, j 0x4A/0x24, k 0x4B/0x25.
#
# PostMessage only: the window is never focused or raised, so a user (or
# another session) working on this desktop is not interrupted.
if (-not ("HbVerifyInput.Win32" -as [type])) {
  Add-Type -Namespace HbVerifyInput -Name Win32 -MemberDefinition '[System.Runtime.InteropServices.DllImport("user32.dll")] public static extern bool PostMessage(System.IntPtr hWnd, uint Msg, System.IntPtr wParam, System.IntPtr lParam);'
}
$p = Get-Process -Id $TargetPid -ErrorAction Stop
$h = $p.MainWindowHandle
if ($h -eq [System.IntPtr]::Zero) { throw "no main window (pid $TargetPid)" }
if ($VK -ne 0) {
  $down = [System.IntPtr](1 -bor ($Scan -shl 16))
  $up = [System.IntPtr]::new((1 -bor ($Scan -shl 16) -bor 0xC0000000))
  [void][HbVerifyInput.Win32]::PostMessage($h, 0x0100, [System.IntPtr]$VK, $down)
  Start-Sleep -Milliseconds 120
  [void][HbVerifyInput.Win32]::PostMessage($h, 0x0101, [System.IntPtr]$VK, $up)
  Write-Output "keydown vk=$VK scan=$Scan -> pid $TargetPid"
}
foreach ($ch in $Chars.ToCharArray()) {
  [void][HbVerifyInput.Win32]::PostMessage($h, 0x0102, [System.IntPtr][int]$ch, [System.IntPtr]::Zero)
  Start-Sleep -Milliseconds $DelayMs
}
if ($Chars.Length -gt 0) { Write-Output ("chars '" + $Chars + "' -> pid " + $TargetPid) }
