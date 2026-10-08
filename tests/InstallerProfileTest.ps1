$ErrorActionPreference = 'Stop'
# Run the installer's actual script against in-memory language lists. These
# replacements prevent writes to real Windows language/keyboard preferences.
$source = Get-Content (Join-Path $PSScriptRoot '..\src\Installer.cpp') -Raw
if ($source -notmatch '(?s)kEnsureUserInputScript\[\]\s*=\s*LR"PS\((.*?)\)PS"') { throw 'Installer script not found' }
$install = [scriptblock]::Create($Matches[1])
$tip = '0C04:{B0F2B76B-8E5B-4A3C-9D1E-7F2A3C4D5E6F}{C1A2B3C4-D5E6-F7A8-B9C0-D1E2F3A4B5C6}'
function Get-WinUserLanguageList { return ,$script:languages }
function Set-WinUserLanguageList($LanguageList, [switch]$Force) {
    $script:writes++
    $script:saved = $LanguageList
}
foreach ($state in @('new-user', 'existing-hk', 'already-installed')) {
    $script:languages = New-WinUserLanguageList 'en-US'
    if ($state -ne 'new-user') { $script:languages.Add('zh-Hant-HK') }
    if ($state -eq 'already-installed') { $script:languages[1].InputMethodTips.Add($tip) }
    $originalEnglish = @($script:languages[0].InputMethodTips)
    $originalHK = if ($script:languages.Count -gt 1) { @($script:languages[1].InputMethodTips) } else { @() }
    $script:writes = 0
    $script:saved = $null
    & $install
    $hk = $null
    foreach ($language in $script:languages) { if ($language.LanguageTag -eq 'zh-Hant-HK') { $hk = $language } }
    if (!$hk -or !($hk.InputMethodTips -contains $tip)) { throw "$state : Yutping not in user's keyboard list" }
    foreach ($item in $originalEnglish) { if (!($script:languages[0].InputMethodTips -contains $item)) { throw 'Existing English input removed' } }
    foreach ($item in $originalHK) { if (!($hk.InputMethodTips -contains $item)) { throw 'Existing Chinese input removed' } }
    if ($state -eq 'already-installed') {
        if ($script:writes -ne 0) { throw 'Repeat install unnecessarily rewrites preferences' }
    } elseif ($script:writes -ne 1 -or !$script:saved) { throw 'Updated keyboard list was not persisted' }
    & $install
    if (@($hk.InputMethodTips | Where-Object { $_ -eq $tip }).Count -ne 1) { throw 'Duplicate input method on repeat install' }
    Write-Output "$state : PASS"
}
