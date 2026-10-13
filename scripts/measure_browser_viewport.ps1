param([string]$Title = 'ZenCrop companion public fixture', [string]$PrepareFixtureWindow = '')
$ErrorActionPreference = 'Stop'
Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;
public static class CompanionWindows {
    public delegate bool Callback(IntPtr window, IntPtr data);
    [StructLayout(LayoutKind.Sequential)] public struct Rect { public int left, top, right, bottom; }
    [DllImport("user32.dll")] public static extern bool EnumWindows(Callback callback, IntPtr data);
    [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr parent, Callback callback, IntPtr data);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr window, out Rect rect);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr window, StringBuilder text, int size);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassName(IntPtr window, StringBuilder text, int size);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr window);
    [DllImport("user32.dll")] public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr context);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr window, out uint pid);
    [DllImport("user32.dll")] public static extern IntPtr PostMessage(IntPtr window, uint message, IntPtr wparam, IntPtr lparam);
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr window, IntPtr after, int x, int y, int w, int h, uint flags);
    public static void PrepareFixture(long fixture) {
        var fixtureWindow = new IntPtr(fixture);
        var title = new StringBuilder(1024); GetWindowText(fixtureWindow, title, title.Capacity);
        if (!title.ToString().Contains("ZenCrop companion public fixture")) throw new InvalidOperationException("Not the public fixture");
        uint fixturePid; GetWindowThreadProcessId(fixtureWindow, out fixturePid);
        EnumWindows((window, data) => {
            uint pid; GetWindowThreadProcessId(window, out pid);
            var text = new StringBuilder(1024); GetWindowText(window, text, text.Capacity);
            if (pid == fixturePid && text.ToString() == "Turn off extensions in developer mode")
                PostMessage(window, 0x0010, IntPtr.Zero, IntPtr.Zero);
            return true;
        }, IntPtr.Zero);
        if (!SetWindowPos(fixtureWindow, new IntPtr(-1), 0, 0, 0, 0, 0x0013))
            throw new InvalidOperationException("Cannot raise the public fixture above other topmost windows");
    }
    public static object[] Measure(string title) {
        var results = new List<object>();
        var old = SetThreadDpiAwarenessContext(new IntPtr(-4));
        EnumWindows((window, data) => {
            var text = new StringBuilder(1024);
            GetWindowText(window, text, text.Capacity);
            if (!text.ToString().Contains(title)) return true;
            Rect physical, logical;
            GetWindowRect(window, out physical);
            SetThreadDpiAwarenessContext(new IntPtr(-1));
            GetWindowRect(window, out logical);
            SetThreadDpiAwarenessContext(new IntPtr(-4));
            uint pid; GetWindowThreadProcessId(window, out pid);
            var children = new List<object>();
            EnumChildWindows(window, (child, unused) => {
                var name = new StringBuilder(256);
                GetClassName(child, name, name.Capacity);
                if (name.ToString() == "Chrome_RenderWidgetHostHWND" && IsWindowVisible(child)) {
                    Rect rect; GetWindowRect(child, out rect);
                    children.Add(new { hwnd = child.ToInt64().ToString(), rect = new[] {rect.left, rect.top, rect.right, rect.bottom} });
                }
                return true;
            }, IntPtr.Zero);
            results.Add(new { hwnd = window.ToInt64().ToString(), pid, title = text.ToString(),
                physical = new[] {physical.left, physical.top, physical.right, physical.bottom},
                logical = new[] {logical.left, logical.top, logical.right, logical.bottom}, viewports = children });
            return true;
        }, IntPtr.Zero);
        SetThreadDpiAwarenessContext(old);
        return results.ToArray();
    }
}
'@
if ($PrepareFixtureWindow) { [CompanionWindows]::PrepareFixture([long]$PrepareFixtureWindow) }
[CompanionWindows]::Measure($Title) | ConvertTo-Json -Depth 6
