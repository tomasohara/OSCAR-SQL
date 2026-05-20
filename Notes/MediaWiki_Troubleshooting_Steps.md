# MediaWiki Character Encoding Troubleshooting

**Question from User:**
We've gotten confused and need to start over looking for this problem. Here is what we know for certain:

We are running Mediawiki 1.28.0, MySQL 5.6.51, and ICU 50.2. Complete version information is at https://www.apneaboard.com/wiki/index.php?title=Special:Version

The special characters look fine when used on the forum, which is also hosted on apneaboard.com. They used to look fine on the Wiki pages but we cannot confirm exactly when the problem first occurred. Data in the wiki database seems correct as we can always re-open a page, edit it, and see the original codes, show preview shows them correctly, but they are not displayed correctly after having been saved.

Website owner says "Everything is as Claude recommends. Neither the OS, PHP, MySQL/MariaDB, or MediaWiki changed at all recently (last time was well over a year ago). Server configs were not changed. There was a database backup done, but that was simply a download of the files. No files were changed."

If there is a clue in the version information, please let us know. Otherwise, please give us a step-by-step list of things to recheck.

---

**Response:**
This is very helpful context. If we trust that absolutely no configuration files or software versions were changed, we must look for a **state change** that occurred around the time of the backup. 

The fact that "Edit" and "Preview" work flawlessly proves that your primary database is healthy and storing the UTF-8 characters correctly. The problem is isolated exclusively to the **rendered output that is served to regular visitors.**

In MediaWiki, that rendered output is stored in the **Parser Cache**. If the cache gets corrupted, or the storage mechanism for the cache changes, regular views will break while edits and previews (which bypass the cache) continue to work perfectly.

Here is a step-by-step list of things the website owner needs to check, ranked by likelihood:

### 1. Test if the Parser Cache is the culprit (The "Proof" Test)
Before changing anything in the database, add this single line to `LocalSettings.php`:
```php
$wgEnableParserCache = false;
```
*   **If the pages instantly display correctly:** You have absolute proof that the parser cache is the culprit. You can remove the line after testing.
*   **If the pages still display mojibake:** The issue is happening during PHP's final output to the browser (meaning something in the server's output buffering or HTTP headers is mangling it, though this is rare if Preview works).

### 2. Check how the database backup was performed
Often, when someone does a database backup, they notice that the `objectcache` or `parsercache` tables are massive. To save space, people will sometimes "Drop" or "Truncate" these cache tables before downloading the backup.
*   **The Check:** If the website owner dropped and recreated the `objectcache` or `parsercache` tables, did they recreate them with the `latin1` character set instead of `binary` or `utf8`? 
*   **The Fix:** Have them open phpMyAdmin and look at the "Collation" or "Type" of the `objectcache` table. If it is `latin1`, MySQL is mangling the cached HTML every time MediaWiki tries to save a page. It must be `binary` or `utf8` / `utf8mb4`.

### 3. Restart external caching services (If applicable)
If your `LocalSettings.php` has `$wgMainCacheType = CACHE_MEMCACHED` (or uses APCu/Redis instead of the database), that external caching service might have crashed or restarted in a strange state during the backup window.
*   **The Check:** Have the server administrator restart the Memcached service (e.g., `sudo service memcached restart` or `systemctl restart memcached`). Sometimes these services get stuck in a state where they strip multi-byte characters.

### 4. Force the Database Connection Encoding
Even if no configuration files were changed manually, sometimes unattended OS security updates (like a minor patch to the server's MySQL client libraries) can quietly change the default character set that PHP uses to talk to MySQL.
*   **The Check:** If PHP is suddenly connecting to MySQL using `latin1` instead of `utf8`, it will cause exactly this issue when saving to the cache.
*   **The Fix:** In MediaWiki 1.28, you can brute-force the connection to ignore server defaults and always use UTF-8 by ensuring this line is in `LocalSettings.php`:
    ```php
    $wgDBmysql5 = true; 
    ```

**Summary of steps to give the owner:**
1. Temporarily add `$wgEnableParserCache = false;` to prove the cache is the problem.
2. Verify the `objectcache` table wasn't accidentally recreated as `latin1` during the backup.
3. Restart Memcached/APCu (if used).
4. Add `$wgDBmysql5 = true;` to guarantee the connection isn't defaulting to Latin-1.
