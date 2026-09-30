# Online search in Rose

Rose can search the public web through the Brave Search API. Obtain a Brave Search API key from Brave, then start Rose from the same PowerShell session:

```powershell
$env:BRAVE_SEARCH_API_KEY = 'your-key-here'
.\build\Debug\Rose.exe
```

The **Search online** menu action is enabled when the key is present. You can also write `Search online for <query>` in chat. Rose shows the exact query for confirmation before sending it to Brave. The tool returns up to five result titles, URLs, and snippets. These snippets are evidence that a search result exists; they do not mean Rose read the linked pages. Keep the key out of chat and source control.

Without a configured key, Rose reports that no search ran. Search requests have an eight-second HTTP receive/send timeout and a bounded response size. A failed search or an empty result set should be reported directly, without fabricated sources.
