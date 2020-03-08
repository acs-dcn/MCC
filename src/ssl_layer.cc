#include "ssl_layer.h"

#include <cstring>
#include <cstdlib>

namespace infgen {

mbedtls_cipher_id_t cipher = MBEDTLS_CIPHER_ID_AES;

const unsigned char key[32] = { 
		0xad,0xc2,0x5f,0x83,0x19,0xb1,0xe2,0xaf,0x11,0x08,0x2c,0x3a,0x2e,0x89,0xe8,0xdf,0xed,0xfc,0x4b,0x55,0xba,0x07,0x11,0x85,0x10,0x87,0xcc,0xbe,0xa6,0x0e,0x30,0xe9};

const unsigned char initial_value[12] = { 
    0x18,0x63,0xd4,0x71,0x12,0x1a,0x74,0x65,0x5a,0x2e,0x2b,0x54};

const unsigned char additional[] = {0x17,0x03,0x03,0x03,0x63};


void ssl_layer::ssl_init(mbedtls_gcm_context &ctx) {
#ifdef AES_GCM
	mbedtls_gcm_init(&ctx);
	mbedtls_gcm_setkey(&ctx, cipher, key, 256);
#endif
}

void ssl_layer::ssl_encrypt(mbedtls_gcm_context ctx, const unsigned char *data, unsigned char* ciphertext, size_t len) {
#ifdef AES_GCM
	unsigned char tag[16];
	ciphertext[0] = 0x17;
	ciphertext[1] = 0x03;
	ciphertext[2] = 0x03;
	ciphertext[3] = 0x03;
	ciphertext[4] = 0x63;

	mbedtls_gcm_crypt_and_tag(&ctx, MBEDTLS_GCM_ENCRYPT, len, initial_value, sizeof(initial_value)/*12*/, additional, sizeof(additional)/*5*/, data/*input*/, (ciphertext + 5)/*output*/, sizeof(tag)/*16*/, tag);

	memcpy(ciphertext + 5 + len, tag, 16); //Additianal + ciphertext + tag
#else
	/// TODO: Add support for dynamic encryption
#endif
}

std::optional<size_t> ssl_layer::ssl_decrypt(mbedtls_gcm_context ctx, const unsigned char *data, unsigned char* plaintext, size_t len) {
#ifdef AES_GCM
	unsigned char tag[16];

	mbedtls_gcm_crypt_and_tag(&ctx, MBEDTLS_GCM_DECRYPT, len, initial_value, sizeof(initial_value)/*12*/, additional, sizeof(additional)/*5*/, data/*input*/, plaintext/*output*/, sizeof(tag)/*16*/, tag);
		
	return {size_t(len)};
#else
		/// TODO: Add support for dynamic decryption
	return {};
#endif
}

}


