<#
.SYNOPSIS
    Captures a window's own pixels to a PNG, without raising or focusing it.

.DESCRIPTION
    Documentation media for this fork has to be captured from a real running
    Terminal, and the only Terminal that may be driven is the Test slot. That
    rules out screen-region grabs: CopyFromScreen reads whatever is physically
    on screen at those coordinates, so an occluded window yields a picture of
    whatever is in front of it, and un-occluding it means taking focus while
    someone is typing.

    PrintWindow asks the window to render itself into a bitmap instead. It is
    read-only, does not move focus, and captures a window sitting behind others.
    PW_RENDERFULLCONTENT (2) is required for a composited window like Terminal;
    without it the terminal surface comes back blank.

    KNOWN LIMIT, observed rather than assumed: the tab row is XAML and always
    captures, but the terminal surface itself is a separate swap chain that the
    renderer only presents to when the window is actually being composited. A
    window that has been fully occluded since it last drew comes back with a
    correct frame and chrome around an empty pane -- and the pane is genuinely
    running, which is easy to confirm over the control pipe while looking at a
    blank picture.

    So a still that must show pane CONTENT needs the window visible when it is
    taken. That means waiting for the machine to be idle, since bringing it
    forward takes focus from whoever is typing. Chrome-only shots, and anything
    where the pane may be empty, work regardless.

.PARAMETER ProcessId
    The process to capture from. Windows are enumerated in Z-order.

.PARAMETER Index
    Which of that process's visible, titled windows to capture. Default 0.

.PARAMETER Out
    Destination .png path. Parent directory is created if needed.

.PARAMETER ListOnly
    Print the process's capturable windows and exit without writing anything.

.EXAMPLE
    ./Capture-Window.ps1 -ProcessId 1234 -ListOnly
    ./Capture-Window.ps1 -ProcessId 1234 -Index 1 -Out docs/media/resume-dark.png
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)][int]$ProcessId,
    [int]$Index = 0,
    [string]$Out,
    [switch]$ListOnly
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

Add-Type @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;
public class CaptureNative {
    public delegate bool EnumProc(IntPtr h, IntPtr l);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc p, IntPtr l);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll")] public static extern int GetWindowTextLength(IntPtr h);
    [DllImport("user32.dll")] public static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }

    public static List<IntPtr> WindowsFor(uint pid) {
        var found = new List<IntPtr>();
        EnumWindows((h, l) => {
            uint owner;
            GetWindowThreadProcessId(h, out owner);
            if (owner == pid && IsWindowVisible(h) && GetWindowTextLength(h) > 0) {
                RECT r;
                GetWindowRect(h, out r);
                // Skip the tiny helper windows Terminal keeps around; only real
                // windows are worth a picture.
                if ((r.R - r.L) > 100 && (r.B - r.T) > 100) { found.Add(h); }
            }
            return true;
        }, IntPtr.Zero);
        return found;
    }

    public static string TitleOf(IntPtr h) {
        var sb = new StringBuilder(512);
        GetWindowText(h, sb, 512);
        return sb.ToString();
    }
}
'@

$handles = [CaptureNative]::WindowsFor([uint32]$ProcessId)
if ($handles.Count -eq 0) {
    throw "No capturable windows for process $ProcessId."
}

if ($ListOnly) {
    for ($i = 0; $i -lt $handles.Count; $i++) {
        $r = New-Object CaptureNative+RECT
        [void][CaptureNative]::GetWindowRect($handles[$i], [ref]$r)
        '{0}  {1}x{2}  {3}' -f $i, ($r.R - $r.L), ($r.B - $r.T), [CaptureNative]::TitleOf($handles[$i])
    }
    return
}

if (-not $Out) { throw "-Out is required unless -ListOnly is given." }
if ($Index -lt 0 -or $Index -ge $handles.Count) {
    throw "Index $Index out of range; process $ProcessId has $($handles.Count) capturable window(s)."
}

$hwnd = $handles[$Index]
$rect = New-Object CaptureNative+RECT
[void][CaptureNative]::GetWindowRect($hwnd, [ref]$rect)
$width = $rect.R - $rect.L
$height = $rect.B - $rect.T

$dir = Split-Path -Parent $Out
if ($dir -and -not (Test-Path $dir)) { New-Item -ItemType Directory -Force $dir | Out-Null }

$bitmap = New-Object System.Drawing.Bitmap $width, $height
$graphics = [System.Drawing.Graphics]::FromImage($bitmap)
try {
    $hdc = $graphics.GetHdc()
    try {
        if (-not [CaptureNative]::PrintWindow($hwnd, $hdc, 2)) {
            throw "PrintWindow failed for window $Index of process $ProcessId."
        }
    }
    finally { $graphics.ReleaseHdc($hdc) }

    $bitmap.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
}
finally {
    $graphics.Dispose()
    $bitmap.Dispose()
}

'{0}  ({1}x{2})' -f $Out, $width, $height
