#pragma once

#include "gcm.h"

#include <functional>
#include <memory>
#include <optional>

namespace infgen {

class ssl_layer {
public:
	mbedtls_gcm_context sctx_;

	ssl_layer() {}
	ssl_layer(mbedtls_gcm_context sctx) : sctx_(sctx) {}

	ssl_layer(const ssl_layer &s) : sctx_(s.sctx_) {};
	ssl_layer(ssl_layer &&s) : sctx_(s.sctx_) {
		mbedtls_gcm_free(&s.sctx_);
	}
	
	
	void operator=(const ssl_layer&) = delete;
	ssl_layer& operator=(const ssl_layer&&s) {
		if (this != &s) {
			sctx_ = s.sctx_;
		}
		return *this;
	}

	mbedtls_gcm_context context() const { return sctx_; }

	static void ssl_init(mbedtls_gcm_context &ctx);

	static int ssl_connect() { return 1; }

	static void ssl_close() { /* Close ssl fd*/ }

	static void ssl_destroy(mbedtls_gcm_context &ctx) {
#ifdef AES_GCM
		// Free gcm context
		mbedtls_gcm_free(&ctx);	
#endif
	} 

	static void ssl_encrypt(mbedtls_gcm_context ctx, const unsigned char *data, unsigned char* ciphertext, size_t len); 

	static std::optional<size_t> ssl_decrypt(mbedtls_gcm_context ctx, const unsigned char *data, unsigned char* plaintext, size_t len); 

};

}
