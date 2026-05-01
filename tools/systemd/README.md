# Systemd user unit (optional)

Auto-starts the localhost stub on login and restarts it if it crashes, so
you never have to think about it after rebooting.

## Install

```bash
mkdir -p ~/.config/systemd/user
cp tools/systemd/fincept-stub.service ~/.config/systemd/user/
systemctl --user daemon-reload
systemctl --user enable --now fincept-stub
```

The unit assumes the repo is at `~/fin/finterm`. Edit `ExecStart=` if yours
lives elsewhere.

## Verify

```bash
systemctl --user status fincept-stub
curl -fs http://127.0.0.1:8765/health
```

## Logs

```bash
journalctl --user -u fincept-stub -f
```

## Disable

```bash
systemctl --user disable --now fincept-stub
```

After disabling, `start.sh` will spin the stub up on demand instead.
