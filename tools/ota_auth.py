# Feeds OTA_PASSWORD from include/secrets.h (gitignored) to espota as --auth.
import re
Import("env")
try:
    text = open("include/secrets.h").read()
    m = re.search(r'^\s*#define\s+OTA_PASSWORD\s+"([^"]*)"', text, re.M)
except OSError:
    m = None
if m:
    env.Append(UPLOADERFLAGS=["--auth=" + m.group(1)])
else:
    print("ota_auth: no OTA_PASSWORD in include/secrets.h; OTA upload will be unauthenticated")
