/**

  @section license License

  Licensed to the Apache Software Foundation (ASF) under one
  or more contributor license agreements.  See the NOTICE file
  distributed with this work for additional information
  regarding copyright ownership.  The ASF licenses this file
  to you under the Apache License, Version 2.0 (the
  "License"); you may not use this file except in compliance
  with the License.  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
 */

#pragma once

#define OPENSSL_THREAD_DEFINES

// BoringSSL does not have this include file
#ifndef OPENSSL_IS_BORINGSSL
#include <openssl/opensslconf.h>
#endif
#include <openssl/ssl.h>

#include "tscore/ink_config.h"
#include "tscore/Diags.h"
#include "records/I_RecCore.h"
#include "P_SSLCertLookup.h"

#include <map>
#include <mutex>
#include <set>
#include <shared_mutex>

struct SSLConfigParams;
class SSLNetVConnection;

typedef int ssl_error_t;

#ifndef OPENSSL_IS_BORINGSSL
typedef int ssl_curve_id;
#else
typedef uint16_t ssl_curve_id;
#endif

// Return the SSL Curve ID associated to the specified SSL connection
ssl_curve_id SSLGetCurveNID(SSL *ssl);

enum class SSLCertContextType;

struct SSLLoadingContext {
  SSL_CTX *ctx;
  SSLCertContextType ctx_type;

  explicit SSLLoadingContext(SSL_CTX *c, SSLCertContextType ctx_type) : ctx(c), ctx_type(ctx_type) {}
};

/** A class for handling TLS secrets logging. */
class TLSKeyLogger
{
public:
  TLSKeyLogger(const TLSKeyLogger &) = delete;
  TLSKeyLogger &operator=(const TLSKeyLogger &) = delete;

  ~TLSKeyLogger()
  {
    std::unique_lock lock{_mutex};
    close_keylog_file();
  }

  /** A callback for TLS secret key logging.
   *
   * This is the callback registered with OpenSSL's SSL_CTX_set_keylog_callback
   * to log TLS secrets if the user enabled that feature. For more information
   * about this callback, see OpenSSL's documentation of
   * SSL_CTX_set_keylog_callback.
   *
   * @param[in] ssl The SSL object associated with the connection.
   * @param[in] line The line to place in the keylog file.
   */
  static void
  ssl_keylog_cb(const SSL *ssl, const char *line)
  {
    instance().log(line);
  }

  /** Return whether TLS key logging is enabled.
   *
   * @return True if TLS session key logging is enabled, false otherwise.
   */
  static bool
  is_enabled()
  {
    return instance()._fd >= 0;
  }

  /** Enable keylogging.
   *
   * @param[in] keylog_file The path to the file to log TLS secrets to.
   */
  static void
  enable_keylogging(const char *keylog_file)
  {
    instance().enable_keylogging_internal(keylog_file);
  }

  /** Disable TLS secrets logging. */
  static void
  disable_keylogging()
  {
    instance().disable_keylogging_internal();
  }

private:
  TLSKeyLogger() = default;

  /** Return the TLSKeyLogger singleton.
   *
   * We use a getter rather than a class static singleton member so that the
   * construction of the singleton delayed until after TLS configuration is
   * processed.
   */
  static TLSKeyLogger &
  instance()
  {
    static TLSKeyLogger instance;
    return instance;
  }

  /** Close the file descriptor for the key log file.
   *
   * @note This assumes that a unique lock has been acquired for _mutex.
   */
  void close_keylog_file();

  /** A TLS secret line to log to the keylog file.
   *
   * @param[in] line A line to log to the keylog file.
   */
  void log(const char *line);

  /** Enable TLS keylogging in the instance singleton. */
  void enable_keylogging_internal(const char *keylog_file);

  /** Disable TLS keylogging in the instance singleton. */
  void disable_keylogging_internal();

private:
  /** A file descriptor for the log file receiving the TLS secrets. */
  int _fd = -1;

  /** A mutex to coordinate dynamically changing TLS logging config changes and
   * logging to the TLS log file. */
  std::shared_mutex _mutex;
};

/**
    @brief Load SSL certificates from ssl_multicert.config and setup SSLCertLookup for SSLCertificateConfig
 */
class SSLMultiCertConfigLoader
{
public:
  struct CertLoadData {
    std::vector<std::string> cert_names_list, key_list, ca_list, ocsp_list;
    std::vector<SSLCertContextType> cert_type_list;
  };
  SSLMultiCertConfigLoader(const SSLConfigParams *p) : _params(p) {}
  virtual ~SSLMultiCertConfigLoader(){};

  bool load(SSLCertLookup *lookup);

  virtual SSL_CTX *default_server_ssl_ctx();

  virtual std::vector<SSLLoadingContext> init_server_ssl_ctx(CertLoadData const &data,
                                                             const SSLMultiCertConfigParams *sslMultCertSettings,
                                                             std::set<std::string> &names);

  static bool load_certs(SSL_CTX *ctx, const std::vector<std::string> &cert_names_list,
                         const std::vector<std::string> &key_names_list, CertLoadData const &data, const SSLConfigParams *params,
                         const SSLMultiCertConfigParams *sslMultCertSettings);

  bool load_certs_and_cross_reference_names(std::vector<X509 *> &cert_list, CertLoadData &data, const SSLConfigParams *params,
                                            const SSLMultiCertConfigParams *sslMultCertSettings,
                                            std::set<std::string> &common_names,
                                            std::unordered_map<int, std::set<std::string>> &unique_names,
                                            SSLCertContextType *certType);

  static bool set_session_id_context(SSL_CTX *ctx, const SSLConfigParams *params,
                                     const SSLMultiCertConfigParams *sslMultCertSettings);

  static int check_server_cert_now(X509 *cert, const char *certname);
  static void clear_pw_references(SSL_CTX *ssl_ctx);

  bool update_ssl_ctx(const std::string &secret_name);

protected:
  const SSLConfigParams *_params;

  bool _store_single_ssl_ctx(SSLCertLookup *lookup, const shared_SSLMultiCertConfigParams &sslMultCertSettings, shared_SSL_CTX ctx,
                             SSLCertContextType ctx_type, std::set<std::string> &names);

private:
  virtual const char *_debug_tag() const;
  virtual bool _store_ssl_ctx(SSLCertLookup *lookup, shared_SSLMultiCertConfigParams ssl_multi_cert_params);
  bool _prep_ssl_ctx(const shared_SSLMultiCertConfigParams &sslMultCertSettings, SSLMultiCertConfigLoader::CertLoadData &data,
                     std::set<std::string> &common_names, std::unordered_map<int, std::set<std::string>> &unique_names);
  virtual void _set_handshake_callbacks(SSL_CTX *ctx);
  virtual bool _setup_session_cache(SSL_CTX *ctx);
  virtual bool _setup_dialog(SSL_CTX *ctx, const SSLMultiCertConfigParams *sslMultCertSettings);
  virtual bool _set_verify_path(SSL_CTX *ctx, const SSLMultiCertConfigParams *sslMultCertSettings);
  virtual bool _setup_session_ticket(SSL_CTX *ctx, const SSLMultiCertConfigParams *sslMultCertSettings);
  virtual bool _setup_client_cert_verification(SSL_CTX *ctx);
  virtual bool _set_cipher_suites_for_legacy_versions(SSL_CTX *ctx);
  virtual bool _set_cipher_suites(SSL_CTX *ctx);
  virtual bool _set_curves(SSL_CTX *ctx);
  virtual bool _set_info_callback(SSL_CTX *ctx);
  virtual bool _set_npn_callback(SSL_CTX *ctx);
  virtual bool _set_alpn_callback(SSL_CTX *ctx);
  virtual bool _set_keylog_callback(SSL_CTX *ctx);
  virtual bool _enable_ktls(SSL_CTX *ctx);
};

// Create a new SSL server context fully configured (cert and keys are optional).
// Used by TS API (TSSslServerContextCreate and TSSslServerCertUpdate)
SSL_CTX *SSLCreateServerContext(const SSLConfigParams *params, const SSLMultiCertConfigParams *sslMultiCertSettings,
                                const char *cert_path = nullptr, const char *key_path = nullptr);

// Release SSL_CTX and the associated data. This works for both
// client and server contexts and gracefully accepts nullptr.
// Used by TS API (TSSslContextDestroy)
void SSLReleaseContext(SSL_CTX *ctx);

// Initialize the SSL library.
void SSLInitializeLibrary();

// Initialize SSL library based on configuration settings
void SSLPostConfigInitialize();

// Attach a SSL NetVC back pointer to a SSL session.
void SSLNetVCAttach(SSL *ssl, SSLNetVConnection *vc);

// Detach from SSL NetVC back pointer to a SSL session.
void SSLNetVCDetach(SSL *ssl);

// Return the SSLNetVConnection (if any) attached to this SSL session.
SSLNetVConnection *SSLNetVCAccess(const SSL *ssl);

void setClientCertLevel(SSL *ssl, uint8_t certLevel);
void setClientCertCACerts(SSL *ssl, const char *file, const char *dir);
void setTLSValidProtocols(SSL *ssl, unsigned long proto_mask, unsigned long max_mask);

// Helper functions to retrieve sni name or ip address from a SSL object
// Used as part of the lookup key into the origin server session cache
std::string get_sni_addr(SSL *ssl);

// Helper functions to retrieve server verify policy and properties from a SSL object
// Used as part of the lookup key into the origin server session cache
std::string get_verify_str(SSL *ssl);

namespace ssl
{
namespace detail
{
  struct SCOPED_X509_TRAITS {
    typedef X509 *value_type;
    static value_type
    initValue()
    {
      return nullptr;
    }
    static bool
    isValid(value_type x)
    {
      return x != nullptr;
    }
    static void
    destroy(value_type x)
    {
      X509_free(x);
    }
  };

  struct SCOPED_BIO_TRAITS {
    typedef BIO *value_type;
    static value_type
    initValue()
    {
      return nullptr;
    }
    static bool
    isValid(value_type x)
    {
      return x != nullptr;
    }
    static void
    destroy(value_type x)
    {
      BIO_free(x);
    }
  };
  /* namespace ssl */ // namespace detail
} /* namespace detail */
} // namespace ssl

struct ats_wildcard_matcher {
  ats_wildcard_matcher()
  {
    if (!regex.compile(R"(^\*\.[^\*.]+)")) {
      Fatal("failed to compile TLS wildcard matching regex");
    }
  }

  ~ats_wildcard_matcher() {}
  bool
  match(const char *hostname) const
  {
    return regex.match(hostname) != -1;
  }

private:
  DFA regex;
};

typedef ats_scoped_resource<ssl::detail::SCOPED_X509_TRAITS> scoped_X509;
typedef ats_scoped_resource<ssl::detail::SCOPED_BIO_TRAITS> scoped_BIO;
