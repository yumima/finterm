#pragma once
// LlmSecureKeys.h — SecureStorage key naming and helpers for LLM API keys.
//
// API keys for LLM providers / custom models / profiles used to live
// in plaintext columns (llm_configs.api_key, llm_models.api_key,
// llm_profiles.api_key).  Migration v022 moves them to SecureStorage
// and NULLs the columns.  Every read path goes through SecureStorage.
//
// The id schemes below are stable across versions — changing them
// requires a re-keying migration.

#include "core/result/Result.h"

#include <QString>

namespace fincept {

/// Returns the SecureStorage id for an `llm_configs` row.
/// Format: "llm.configs.<provider>"
QString llm_secure_key_for_provider(const QString& provider);

/// Returns the SecureStorage id for an `llm_models` row.
/// Format: "llm.models.<id>"
QString llm_secure_key_for_model(const QString& model_id);

/// Returns the SecureStorage id for an `llm_profiles` row.
/// Format: "llm.profiles.<id>"
QString llm_secure_key_for_profile(const QString& profile_id);

/// Retrieve a key from SecureStorage.
/// Returns empty string when the entry is missing or any retrieve error
/// occurs (logged at debug level).  Callers treat empty as "no key
/// configured" and surface a banner to the user.
QString llm_secure_load(const QString& key);

/// Store a key in SecureStorage. Empty `value` triggers `llm_secure_remove`.
Result<void> llm_secure_store(const QString& key, const QString& value);

/// Remove a key from SecureStorage. Not-found is treated as success.
Result<void> llm_secure_remove(const QString& key);

} // namespace fincept
