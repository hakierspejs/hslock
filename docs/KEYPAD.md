# Keypad Usage

## Input Format

To open the door, enter your key ID followed by your 6-digit TOTP code, then press `#`:

```plain
[1-3 digit key ID][6 digit TOTP code]#
```

**Examples:**

- Key 1, code 123456 → `1123456#`
- Key 42, code 000001 → `42000001#`
- Key 255, code 999999 → `255999999#`

## Special Keys

| Key | Action |
| ----- | -------- |
| `#` | Submit input |
| `*` | Clear input buffer |
| `A` | Ring doorbell |

## Feedback

| Event | Buzzer |
| ------- | -------- |
| Key press | Short beep |
| Access granted | Success melody + latch opens |
| Access denied | Fail melody |
| Doorbell | Doorbell melody |
| Input cleared | Short beep |
