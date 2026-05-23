> We have a growing body of user documentation for OSCAR on a mediawiki server. We implement a connection using the Help/Online User Guide menu item which launches the user's default browser with QDesktopServices::openUrl. If we wanted to put help links in other places, we could obviously do the same with a different URL for each. Is there any practical way of avoiding open yet another browser with each link?

● Yes — the practical solution is a persistent QWebEngineView help window inside OSCAR itself. You'd create a singleton-style HelpBrowser window (or dockable widget) that holds a QWebEngineView. When any help link is clicked, instead of calling QDesktopServices::openUrl(), you call something like HelpBrowser::instance()->show(url) — which shows the window if hidden and navigates to the new URL. Only one window ever exists, and it reuses the same embedded browser session across all help links.

The main trade-off: Qt WebEngine is a large dependency. It bundles Chromium, adding ~100–200 MB to your distribution. On Windows that's one set of DLLs; on Mac/Linux, similar weight. For 25 target environments you'd need to verify packaging for each platform.

If the size is acceptable, the implementation is straightforward — add QT += webenginewidgets to oscar.pro, create a simple HelpBrowser widget class, and swap out the openUrl call. The wiki renders exactly as it would in Chrome/Edge since Qt WebEngine is Chromium-based.

If you want to avoid the Chromium dependency, the only realistic alternative is accepting multiple browser tabs and relying on the browser's "open URL in existing window" behavior — which you can't control from Qt's side.



The main options are:

    1. Embedded QWebEngineView (Chromium-based, ~100-200MB added to distribution) — one persistent window inside OSCAR, no external browser at all.
  2. QDesktopServices::openUrl() (current approach) — opens external browser; no control over tab reuse from Qt's side.
    3. Qt's built-in help system (QtHelp / QHelpEngine) — displays pre-compiled .qch files offline. Not practical for a live MediaWiki.

Those are essentially the options. There's no lightweight middle ground that reliably reuses an existing browser window from Qt.