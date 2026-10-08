param([string]$OutputPath = (Join-Path ([Environment]::GetFolderPath('Desktop')) 'YutpingDiagnostics.txt'))

# Read installation state only. Does not enable/disable profiles, change
# registry values or language preferences, send keys, or access the network.
$report = New-Object 'System.Collections.Generic.List[string]'
function Record($text) { $report.Add([string]$text) }
function RegistryReport($path) {
    Record "Registry: $path"
    if (!(Test-Path -LiteralPath $path)) { Record '  MISSING'; return }
    $properties = Get-ItemProperty -LiteralPath $path -ErrorAction Stop
    foreach ($property in $properties.PSObject.Properties) {
        if ($property.Name -notmatch '^PS') { Record "  $($property.Name) = $($property.Value)" }
    }
}

Record 'Yutping IME diagnostics (no typed text collected)'
Record "Time: $(Get-Date -Format o)"
Record "Process bits: $([IntPtr]::Size * 8)"
try {
    $architecture = (Get-ItemProperty 'HKLM:\SYSTEM\CurrentControlSet\Control\Session Manager\Environment' -Name PROCESSOR_ARCHITECTURE -ErrorAction Stop).PROCESSOR_ARCHITECTURE
    Record "OS architecture: $architecture"
    $os = Get-ItemProperty 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion'
    Record "Windows: $($os.ProductName); build $($os.CurrentBuild).$($os.UBR)"
    $languageList = Get-WinUserLanguageList
    foreach ($language in $languageList) {
        Record "Language: $($language.LanguageTag); InputMethodTips: $($language.InputMethodTips -join ', ')"
    }
    $clsid = '{B0F2B76B-8E5B-4A3C-9D1E-7F2A3C4D5E6F}'
    $profile = '{C1A2B3C4-D5E6-F7A8-B9C0-D1E2F3A4B5C6}'
    $comKey = "HKLM:\SOFTWARE\Classes\CLSID\$clsid\InprocServer32"
    RegistryReport $comKey
    # HKCU entries can override machine-wide COM registration.
    RegistryReport "HKCU:\SOFTWARE\Classes\CLSID\$clsid\InprocServer32"
    foreach ($hive in @('HKLM:', 'HKCU:')) {
        $tip = "$hive\SOFTWARE\Microsoft\CTF\TIP\$clsid"
        RegistryReport "$tip\LanguageProfile\0x00000c04\$profile"
        $categories = "$tip\Category\Item\$clsid"
        Record "Categories: $categories"
        Get-ChildItem -LiteralPath $categories -ErrorAction SilentlyContinue |
            ForEach-Object { Record "  $($_.PSChildName)" }
    }
    $dll = (Get-ItemProperty -LiteralPath "Registry::HKEY_CLASSES_ROOT\CLSID\$clsid\InprocServer32" -ErrorAction SilentlyContinue).'(default)'
    Record "Effective DLL: $dll"
    if ($dll -and (Test-Path -LiteralPath $dll -PathType Leaf)) {
        Record "DLL SHA256: $((Get-FileHash -LiteralPath $dll -Algorithm SHA256).Hash)"
        $signature = Get-AuthenticodeSignature -LiteralPath $dll
        Record "DLL signature: $($signature.Status)"
        $stream = [IO.File]::OpenRead($dll)
        try {
            $reader = New-Object IO.BinaryReader($stream)
            $stream.Position = 0x3c
            $peOffset = $reader.ReadInt32()
            $stream.Position = $peOffset
            if ($reader.ReadUInt32() -ne 0x4550) { throw 'Invalid PE signature' }
            Record ('DLL machine: 0x{0:X4} (8664=x64; AA64=ARM64; 014C=x86)' -f $reader.ReadUInt16())
        } finally { $stream.Dispose() }
    } else { Record 'DLL file: MISSING' }

    # Create and release the installed COM service in this diagnostic process.
    # This checks loading/interface availability, not document input or app
    # container compatibility. No Activate call or global input state changes.
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class YutpingLoadProbe {
    [DllImport("ole32.dll")]
    static extern int CoInitializeEx(IntPtr reserved, uint flags);
    [DllImport("ole32.dll")]
    static extern void CoUninitialize();
    [DllImport("ole32.dll")]
    static extern int CoCreateInstance(ref Guid clsid, IntPtr outer, uint context, ref Guid iid, out IntPtr service);
    public static string Check() {
        int initialized = CoInitializeEx(IntPtr.Zero, 2);
        if (initialized < 0 && initialized != unchecked((int)0x80010106))
            return "COM initialization: 0x" + initialized.ToString("X8");
        IntPtr service = IntPtr.Zero;
        try {
            Guid clsid = new Guid("B0F2B76B-8E5B-4A3C-9D1E-7F2A3C4D5E6F");
            Guid iid = new Guid("AA80E7F7-2021-11D2-93E0-0060B067B86E");
            int hr = CoCreateInstance(ref clsid, IntPtr.Zero, 1, ref iid, out service);
            return "COM load / ITfTextInputProcessor: 0x" + hr.ToString("X8") +
                (hr >= 0 ? " (PASS; activation in target app still untested)" : " (FAIL)");
        } finally {
            if (service != IntPtr.Zero) Marshal.Release(service);
            if (initialized >= 0) CoUninitialize();
        }
    }
}
'@
    Record ([YutpingLoadProbe]::Check())
} catch { Record "Diagnostic error: $($_.Exception.Message)" }

$activationLog = Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) 'YutpingIME\activation.log'
Record 'Recent activation stages (no keystrokes or spelling):'
if (Test-Path -LiteralPath $activationLog) {
    Get-Content -LiteralPath $activationLog -Tail 60 | ForEach-Object { Record $_ }
} else { Record '  No activation log. Older releases do not record these stages.' }

foreach ($log in @('Application', 'Microsoft-Windows-CodeIntegrity/Operational')) {
    Record "Recent Yutping events: $log"
    try {
        $events = @(Get-WinEvent -FilterHashtable @{ LogName=$log; StartTime=(Get-Date).AddDays(-2) } -MaxEvents 300 -ErrorAction Stop |
            Where-Object { $_.Message -match '(?i)Yutping' } | Select-Object -First 10)
        if (!$events.Count) { Record '  No matching events among the latest 300 events in the last two days.' }
        foreach ($event in $events) { Record "  $($event.TimeCreated) ID=$($event.Id) $($event.Message)" }
    } catch { Record "  Log unavailable or no recent events: $($_.Exception.Message)" }
}
$report | Set-Content -LiteralPath $OutputPath -Encoding UTF8
Write-Host "Report saved: $OutputPath"
Write-Host 'Send YutpingDiagnostics.txt to the developer. Local install paths and language settings are included.'
