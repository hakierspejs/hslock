# Serial Console Commands

Connect via serial USB (screen or PuTTY).

```sh
screen /dev/ttyACM0 115200
```

```sh
putty -serial /dev/ttyACM0 -sercfg 115200,8,n,1,N
```

Commands marked **admin** require `login` first.

## General

| Command | Access | Description |
| --------- | -------- | ------------- |
| `help` / `?` | user | Print available commands for current access level |
| `status` | user | Show mode, build info, uptime, board ID, WiFi, NTP, key count |
| `test` | user | Blink LED, activate light, open latch |
| `get-time` | user | Show current RTC time and last NTP sync timestamp |
| `login <key_id> <totp_code>` | user | Enable admin mode using TOTP from an admin-flagged key |
| `logout` | admin | End admin session |
| `reboot` | admin | Reboot device |

## Network

| Command | Access | Description |
| --------- | -------- | ------------- |
| `set-wifi <ssid> <password>` | admin | Save WiFi credentials, reconnect and resync NTP |
| `sync-ntp` | admin | Force immediate NTP resync |

## Key Management

| Command | Access | Description |
| --------- | -------- | ------------- |
| `list-keys` | admin | List all keys: id, status, name, enabled, admin, created |
| `get-key <id>` | admin | Show key details including raw secret bytes |
| `get-key-secret <id>` | admin | Show base32 secret, otpauth:// URI and ASCII QR code for enrollment |
| `add-key <id> <name>` | admin | Generate random secret and save new key. Error if id exists. |
| `rename-key <id> <name>` | admin | Update key label without touching the secret |
| `disable-key <id>` | admin | Disable key without deleting it |
| `enable-key <id>` | admin | Re-enable a disabled key |
| `delete-key <id>` | admin | Permanently delete a key |
| `set-key-admin <id>` | admin | Grant admin flag — key's TOTP accepted by `login` |
| `unset-key-admin <id>` | admin | Remove admin flag from key |

## Backup

| Command | Access | Description |
| --------- | -------- | ------------- |
| `export-keys` | admin | Dump all keys as base64-encoded binary to serial |
| `import-keys <base64>` | admin | Export current keys as backup, then overwrite with provided data |

## Notes

- Key IDs must be in range **0–255**
- Admin session ends on `logout` or USB disconnect
- Login is open (no TOTP required) when: no WiFi configured, no admin keys exist, or RTC not set after 5 minutes of uptime
