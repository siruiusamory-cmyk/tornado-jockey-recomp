# Privacy

Tornado Jockey Recomp collects nothing: no telemetry, no analytics, no crash upload, no
identifier. The proxy reads one local settings file and forwards the game's own calls.

The stock game is different: `Tornado.exe` creates an embedded browser control at start-up and
fetches a storefront page from its 2006 publisher, which is why a stock launch shows Script
Error dialogs offline. The port adds nothing to that request. A scripted run
(`tools/proxy-run.ps1` and both gates) answers the boot fetch offline, so no request leaves the
machine.

Logs and captures are written only where a run is told to write them, stay on your machine and
are never sent anywhere.
