' Launch a command with genuinely no visible window.
'
' -WindowStyle Hidden on pwsh.exe/powershell.exe is not enough on a machine that
' has Windows Terminal set as its default terminal-delegation host (HKCU\Console\
' %%Startup\DelegationTerminal): console-app window creation gets handed off to a
' new Windows Terminal window before the target process's own hidden-window
' request can suppress it, so a "hidden" scheduled task still flashes a window.
'
' WScript.Shell.Run's hidden flag goes through ShellExecute at a layer that does
' not trigger that console-delegation handoff, which is why routing through here
' actually stays invisible where -WindowStyle Hidden alone does not.
'
' Usage: wscript.exe Invoke-Hidden.vbs <exe> [arg] [arg] ...
' Pass the executable and each of its arguments as separate wscript.exe argv
' elements (standard Windows command-line quoting handles any that contain
' spaces); every one is individually re-quoted here before being rejoined, so
' none of that quoting has to survive being flattened into a single string.
Dim shell, cmd, i
Set shell = CreateObject("WScript.Shell")
cmd = ""
For i = 0 To WScript.Arguments.Count - 1
    If i > 0 Then cmd = cmd & " "
    cmd = cmd & """" & WScript.Arguments(i) & """"
Next
shell.Run cmd, 0, False
