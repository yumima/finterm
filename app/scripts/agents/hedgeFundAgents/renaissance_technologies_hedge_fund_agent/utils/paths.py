"""
Cross-Platform Path Utilities for finterm

Provides consistent paths across Windows, macOS, and Linux for storing
RenTech agent data (knowledge base, memory, logs, etc.)

Everything lives under the app's own data folder, in features/renaissance
(see finterm_paths) — the one place the user manages finterm's data.
"""

import os
import platform
from pathlib import Path
from typing import Optional
import logging
from finterm_paths import feature_dir

logger = logging.getLogger(__name__)


def get_app_data_dir() -> Path:
    """
    Get the finterm application data directory.

    Returns the appropriate directory for the current platform:
    - Windows: %APPDATA%\finterm\
    - macOS: ~/Library/Application Support/finterm/
    - Linux: ~/.local/share/finterm/

    Creates the directory if it doesn't exist.

    Returns:
        Path to finterm app data directory
    """
    system = platform.system()

    if system == "Windows":
        # Windows: %APPDATA%\finterm
        base = Path(os.getenv("APPDATA", os.path.expanduser("~/AppData/Roaming")))
    elif system == "Darwin":
        # macOS: ~/Library/Application Support/finterm
        base = Path.home() / "Library" / "Application Support"
    else:
        # Linux: ~/.local/share/finterm
        base = Path.home() / ".local" / "share"

    app_dir = feature_dir("renaissance")

    # Create directory if it doesn't exist
    try:
        app_dir.mkdir(parents=True, exist_ok=True)
    except Exception as e:
        logger.warning(f"Could not create app data directory: {e}")

    return app_dir


def get_rentech_data_dir() -> Path:
    """
    Get the RenTech agent data directory.

    Returns:
        Path to rentech agent data directory
        (e.g., %APPDATA%\finterm\rentech\)
    """
    rentech_dir = get_app_data_dir() / "rentech"

    try:
        rentech_dir.mkdir(parents=True, exist_ok=True)
    except Exception as e:
        logger.warning(f"Could not create RenTech data directory: {e}")

    return rentech_dir


def get_knowledge_base_path() -> Path:
    """
    Get the path for knowledge base storage (LanceDB).

    Returns:
        Path to knowledge base directory
        (e.g., %APPDATA%\finterm\rentech\knowledge\)
    """
    kb_dir = get_rentech_data_dir() / "knowledge"

    try:
        kb_dir.mkdir(parents=True, exist_ok=True)
    except Exception as e:
        logger.warning(f"Could not create knowledge base directory: {e}")

    return kb_dir


def get_memory_db_path() -> Path:
    """
    Get the path for memory database (SQLite).

    Returns:
        Path to memory database file
        (e.g., %APPDATA%\finterm\rentech\memory.db)
    """
    return get_rentech_data_dir() / "memory.db"


def get_logs_dir() -> Path:
    """
    Get the path for logs directory.

    Returns:
        Path to logs directory
        (e.g., %APPDATA%\finterm\rentech\logs\)
    """
    logs_dir = get_rentech_data_dir() / "logs"

    try:
        logs_dir.mkdir(parents=True, exist_ok=True)
    except Exception as e:
        logger.warning(f"Could not create logs directory: {e}")

    return logs_dir


def get_cache_dir() -> Path:
    """
    Get the path for cache directory.

    Returns:
        Path to cache directory
        (e.g., %APPDATA%\finterm\rentech\cache\)
    """
    cache_dir = get_rentech_data_dir() / "cache"

    try:
        cache_dir.mkdir(parents=True, exist_ok=True)
    except Exception as e:
        logger.warning(f"Could not create cache directory: {e}")

    return cache_dir


def get_frontend_db_path() -> Optional[Path]:
    """
    Get the path to frontend SQLite database (fincept.db).

    Searches common locations for the frontend database.

    Returns:
        Path to fincept.db if found, None otherwise
    """
    # Check environment variable first
    env_path = os.getenv("FINTERM_DB_PATH")
    if env_path:
        path = Path(env_path)
        if path.exists():
            return path

    # Check standard location
    app_data = get_app_data_dir()
    db_path = app_data / "fincept.db"
    if db_path.exists():
        return db_path

    # Alternative locations
    alternatives = [
        app_data.parent / "fincept.db",  # One level up
        Path.home() / ".fincept" / "fincept.db",
        Path("./fincept.db"),  # Current directory (dev mode)
    ]

    for path in alternatives:
        if path.exists():
            logger.info(f"Found frontend database at: {path}")
            return path

    logger.warning("Frontend database (fincept.db) not found")
    return None


# Export convenient path getters
def get_knowledge_path() -> str:
    """Get knowledge base path as string"""
    return str(get_knowledge_base_path())


def get_memory_path() -> str:
    """Get memory database path as string"""
    return str(get_memory_db_path())


def get_log_file_path(log_name: str = "rentech.log") -> str:
    """Get log file path as string"""
    return str(get_logs_dir() / log_name)


def get_cache_file_path(cache_name: str) -> str:
    """Get cache file path as string"""
    return str(get_cache_dir() / cache_name)


# Display paths for debugging
def print_paths():
    """Print all configured paths (for debugging)"""
    print("=== finterm - RenTech Agent Paths ===")
    print(f"Platform: {platform.system()}")
    print(f"App Data Dir: {get_app_data_dir()}")
    print(f"RenTech Data Dir: {get_rentech_data_dir()}")
    print(f"Knowledge Base: {get_knowledge_base_path()}")
    print(f"Memory DB: {get_memory_db_path()}")
    print(f"Logs Dir: {get_logs_dir()}")
    print(f"Cache Dir: {get_cache_dir()}")
    print(f"Frontend DB: {get_frontend_db_path()}")
    print("=" * 50)


if __name__ == "__main__":
    # Test paths when run directly
    print_paths()
