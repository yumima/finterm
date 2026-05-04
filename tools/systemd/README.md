# Systemd user unit (optional)

Auto-starts the localhost stub on login and restarts it if it crashes, so
you never have to think about it after rebooting.

## Install

```bash
mkdir -p ~/.config/systemd/user
cp tools/systemd/finterm-stub.service ~/.config/systemd/user/
systemctl --user daemon-reload
systemctl --user enable --now finterm-stub
```

The unit assumes the repo is at `~/fin/finterm`. Edit `ExecStart=` if yours
lives elsewhere.

## Verify

```bash
systemctl --user status finterm-stub
curl -fs http://127.0.0.1:8765/health
```

## Logs

```bash
journalctl --user -u finterm-stub -f
```

## Disable

```bash
systemctl --user disable --now finterm-stub
```

After disabling, `finterm.sh` will spin the stub up on demand instead.

## Migrating from `fincept-stub.service`

If you previously installed the unit under its old name:

```bash
systemctl --user disable --now fincept-stub
rm ~/.config/systemd/user/fincept-stub.service
# then follow Install above
```
