/******************************************************************************
 *                       ____    _    _____                                   *
 *                      / ___|  / \  |  ___|    C++                           *
 *                     | |     / _ \ | |_       Actor                         *
 *                     | |___ / ___ \|  _|      Framework                     *
 *                      \____/_/   \_|_|                                      *
 *                                                                            *
 * Copyright (C) 2011 - 2017                                                  *
 * Dominik Charousset <dominik.charousset (at) haw-hamburg.de>                *
 *                                                                            *
 * Distributed under the terms and conditions of the BSD 3-Clause License or  *
 * (at your option) under the terms and conditions of the Boost Software      *
 * License 1.0. See accompanying files LICENSE and LICENSE_ALTERNATIVE.       *
 *                                                                            *
 * If you did not receive a copy of the license files, see                    *
 * http://opensource.org/licenses/BSD-3-Clause and                            *
 * http://www.boost.org/LICENSE_1_0.txt.                                      *
 ******************************************************************************/

#include "caf/openssl/session.hpp"

#include <sys/socket.h>

#include <openssl/err.h>

#include "caf/actor_system_config.hpp"
#include "caf/openssl/manager.hpp"

namespace caf {
namespace openssl {

namespace {

int pem_passwd_cb(char* buf, int size, int, void* ptr) {
  auto passphrase = reinterpret_cast<session*>(ptr)->openssl_passphrase();
  strncpy(buf, passphrase, size);
  buf[size - 1] = '\0';
  return strlen(buf);
}

} // namespace <anonymous>

session::session(actor_system& sys) : sys_(sys) {
  // nop
}

void session::init() {
  ctx_ = create_ssl_context();
  ssl_ = SSL_new(ctx_);
  if (!ssl_)
    CAF_RAISE_ERROR("cannot create SSL session");
}

session::~session() {
  SSL_free(ssl_);
  SSL_CTX_free(ctx_);
}

bool session::read_some(size_t& result, native_socket /* fd */, void* buf,
                        size_t len) {
  CAF_LOG_TRACE(CAF_ARG(fd) << CAF_ARG(len));
  if (len == 0)
    return 0;
  auto ret = SSL_read(ssl_, buf, len);
  if (ret > 0) {
    result = ret;
    return true;
  }
  result = 0;
  return handle_ssl_result(ret);
}

bool session::write_some(size_t& result, native_socket /* fd */,
                         const void* buf, size_t len) {
  CAF_LOG_TRACE(CAF_ARG(fd) << CAF_ARG(len));
  if (len == 0)
    return true;
  auto ret = SSL_write(ssl_, buf, len);
  if (ret > 0) {
    result = static_cast<size_t>(ret);
    return true;
  }
  result = 0;
  return handle_ssl_result(ret);
}

bool session::connect(native_socket fd) {
  init();
  SSL_set_fd(ssl_, fd);
  SSL_set_connect_state(ssl_);
  auto ret = SSL_connect(ssl_);
  return ret > 0 || handle_ssl_result(ret);
}

bool session::try_accept(native_socket fd) {
  init();
  SSL_set_fd(ssl_, fd);
  SSL_set_accept_state(ssl_);
  auto ret = SSL_accept(ssl_);
  return ret > 0 || handle_ssl_result(ret);
}

const char* session::openssl_passphrase() {
  return openssl_passphrase_.c_str();
}

SSL_CTX* session::create_ssl_context() {
  auto ctx = SSL_CTX_new(TLSv1_2_method());
  if (!ctx)
    raise_ssl_error("cannot create OpenSSL context");
  if (sys_.openssl_manager().authentication_enabled()) {
    // Require valid certificates on both sides.
    auto& cfg = sys_.config();
    if (cfg.openssl_certificate.size() > 0
        && SSL_CTX_use_certificate_chain_file(ctx,
                                              cfg.openssl_certificate.c_str())
             != 1)
      raise_ssl_error("cannot load certificate");
    if (cfg.openssl_passphrase.size() > 0) {
      openssl_passphrase_ = cfg.openssl_passphrase;
      SSL_CTX_set_default_passwd_cb(ctx, pem_passwd_cb);
      SSL_CTX_set_default_passwd_cb_userdata(ctx, (void*)this);
    }
    if (cfg.openssl_key.size() > 0
        && SSL_CTX_use_PrivateKey_file(ctx, cfg.openssl_key.c_str(),
                                       SSL_FILETYPE_PEM)
             != 1)
      raise_ssl_error("cannot load private key");
    auto cafile =
      (cfg.openssl_cafile.size() > 0 ? cfg.openssl_cafile.c_str() : nullptr);
    auto capath =
      (cfg.openssl_capath.size() > 0 ? cfg.openssl_capath.c_str() : nullptr);
    if (cafile || capath) {
      if (SSL_CTX_load_verify_locations(ctx, cafile, capath) != 1)
        raise_ssl_error("cannot load trusted CA certificates");
    }
    SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT,
                       nullptr);
    if (SSL_CTX_set_cipher_list(ctx, "HIGH:!aNULL:!MD5") != 1)
      raise_ssl_error("cannot set cipher list");
  } else {
    // No authentication.
    SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, nullptr);
    auto ecdh = EC_KEY_new_by_curve_name(NID_secp384r1);
    if (!ecdh)
      raise_ssl_error("cannot get ECDH curve");
    SSL_CTX_set_tmp_ecdh(ctx, ecdh);
    if (SSL_CTX_set_cipher_list(ctx, "AECDH-AES256-SHA") != 1)
      raise_ssl_error("cannot set anonymous cipher");
  }
  return ctx;
}

std::string session::get_ssl_error() {
  std::string msg = "";
  while (auto err = ERR_get_error()) {
    if (msg.size() > 0)
      msg += " ";
    char buf[256];
    ERR_error_string_n(err, buf, sizeof(buf));
    msg += buf;
  }
  return msg;
}

void session::raise_ssl_error(std::string msg) {
  CAF_RAISE_ERROR(std::string("[OpenSSL] ") + msg + ": " + get_ssl_error());
}

bool session::handle_ssl_result(int ret) {
  auto err = SSL_get_error(ssl_, ret);
  switch (err) {
    case SSL_ERROR_WANT_READ: // Try again next time.
    case SSL_ERROR_WANT_WRITE:
      return true;
    case SSL_ERROR_ZERO_RETURN: // Regular remote connection shutdown.
    case SSL_ERROR_SYSCALL:     // Socket connection closed.
      return false;
    default: // Other error
      // TODO: Log somehow?
      // auto msg = get_ssl_error();
      return false;
  }
}

} // namespace openssl
} // namespace caf
