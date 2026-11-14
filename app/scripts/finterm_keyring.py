"""finterm's keyring entries, for the Python tools — the one place that knows
the service/attribute names (AppIdentity's kKeyring*; SecureStorage.cpp).

Values are never printed. Readers fall back to an older build's identity until
the app's first launch of this version moves the entries.
"""

CURRENT = ("finterm", "finterm-key")
LEGACY = ("com.fincept.terminal", "fincept-key")
IDENTITIES = (CURRENT, LEGACY)


class KeyringUnavailable(RuntimeError):
    """libsecret missing, or the keyring service could not be reached."""


def _secret():
    try:
        import gi

        gi.require_version("Secret", "1")
        from gi.repository import Secret
    except Exception as e:  # noqa: BLE001 — any import failure means "no keyring"
        raise KeyringUnavailable(f"libsecret not available ({type(e).__name__})") from None
    return Secret


def schema(identity=CURRENT):
    Secret = _secret()
    return Secret.Schema.new(identity[0], Secret.SchemaFlags.NONE,
                             {identity[1]: Secret.SchemaAttributeType.STRING})


def get(name, legacy_names=()):
    """The value stored under @name (or an older key name), current identity
    first; None if absent. Raises KeyringUnavailable if it could not look."""
    Secret = _secret()
    for ident in IDENTITIES:
        for key in (name, *legacy_names):
            try:
                v = Secret.password_lookup_sync(schema(ident), {ident[1]: key}, None)
            except Exception as e:  # noqa: BLE001
                raise KeyringUnavailable(f"keyring lookup failed ({type(e).__name__})") from None
            if v:
                return v
    return None


def set(name, value):  # noqa: A001 — mirrors get()
    """Store @value under @name with SecureStorage's label and collection, so
    the app sees it like any key it stored itself."""
    Secret = _secret()
    Secret.password_store_sync(schema(), {CURRENT[1]: name}, Secret.COLLECTION_DEFAULT,
                               f"finterm: {name}", value, None)


def all_values(min_len=12):
    """Every stored value at least @min_len long (for the pre-commit guard)."""
    Secret = _secret()
    flags = Secret.SearchFlags.ALL | Secret.SearchFlags.LOAD_SECRETS | Secret.SearchFlags.UNLOCK
    out = []
    for ident in IDENTITIES:
        for it in Secret.password_search_sync(schema(ident), {}, flags, None):
            sec = it.get_secret()
            val = sec.get_text() if sec else None
            if val and len(val) >= min_len:
                out.append(val)
    return out
