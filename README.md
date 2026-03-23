# esp32-core
Base ESP32 firmware that downloads and installs the project-specific firmware OTA

## OTA hash requirement

Project OTA manifest files must include a SHA-256 hash for the binary.
OTA now fails closed if the hash is missing, malformed, or does not match.

Store the expected hash in each project/channel manifest at:

`https://firmware.sven.lol/<project>/<channel>/manifest.json`

Required manifest format:

```json
{
	"version": "1.2.3",
	"bin": "https://firmware.sven.lol/<project>/<channel>/firmware.bin",
	"sha256": "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
}
```
