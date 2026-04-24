# nightly-notify.ps1
# Shows a Windows toast notification. Called by nightly-build.bat on failure.
# Usage: nightly-notify.ps1 "message text"
param([string]$Message)

[Windows.UI.Notifications.ToastNotificationManager, Windows.UI.Notifications, ContentType=WindowsRuntime] | Out-Null

$xml = [Windows.UI.Notifications.ToastNotificationManager]::GetTemplateContent(
    [Windows.UI.Notifications.ToastTemplateType]::ToastText01)
$xml.GetElementsByTagName("text")[0].AppendChild($xml.CreateTextNode($Message)) | Out-Null

[Windows.UI.Notifications.ToastNotificationManager]::CreateToastNotifier("OSCAR Nightly Build").Show(
    [Windows.UI.Notifications.ToastNotification]::new($xml))
