/*
	Copyright (c) 2012-2013 Christopher A. Taylor.  All rights reserved.

	Redistribution and use in source and binary forms, with or without
	modification, are permitted provided that the following conditions are met:

	* Redistributions of source code must retain the above copyright notice,
	  this list of conditions and the following disclaimer.
	* Redistributions in binary form must reproduce the above copyright notice,
	  this list of conditions and the following disclaimer in the documentation
	  and/or other materials provided with the distribution.
	* Neither the name of Calico nor the names of its contributors may be used
	  to endorse or promote products derived from this software without
	  specific prior written permission.

	THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
	AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
	IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
	ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
	LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
	CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
	SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
	INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
	CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
	ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
	POSSIBILITY OF SUCH DAMAGE.
*/

#ifndef CAT_CALICO_HPP
#define CAT_CALICO_HPP

#include "AntiReplayWindow.hpp"
#include "ChaChaVMAC.hpp"

/*
 * Calico Authenticated Encrypted Tunnel
 *
 * Provides symmetric encryption, message authentication, and replay protection
 * for a secure tunnel over an untrusted network between two endpoints with a
 * simple, high-performance design.
 *
 * Designed to be used with UDP sockets and not for TCP streams.
 *
 * Not thread-safe.  Do not share a Calico object between threads without
 * using a full memory barrier.
*/

namespace cat {

namespace calico {


// Return codes for Calico functions
enum CalicoResult
{
	ERR_GROOVY = 0,			// No error

	ERR_BAD_STATE = -1,		// Need to successfully call Initialize() first
	ERR_BAD_INPUT = -2,		// Input parameter(s) are invalid
	ERR_INTERNAL = -3,		// Internal error
	ERR_TOO_SMALL = -4,		// Input buffer is too small
	ERR_IV_DROP = -5,		// Message IV was not accepted (replayed or old)
	ERR_MAC_DROP = -6,		// Message authentication check failed

	ERR_UNKNOWN = -7
};

// Modes to pass to Calico::Initialize()
enum CalicoModes
{
	INITIATOR = 1,
	RESPONDER = 2,
};


//// Calico

class CAT_EXPORT Calico
{
	bool _initialized;
	AntiReplayWindow _window;
	ChaChaVMAC _cipher;

public:
	Calico();

	/*
	 * string = GetErrorString(error_code)
	 *
	 * Returns a pointer to an ASCII character string for the given error code.
	 */
	static const char *GetErrorString(int error_code);

	static const int OVERHEAD = 11; // 11 Bytes are added per message

	/*
	 * result = Initialize(key, session_name, mode)
	 *
	 * Initializes the authenticated encryption.  Call this function before
	 * calling Encrypt() or Decrypt().
	 *
	 *  key: Pointer to the 32 byte secret encryption key
	 *  session_name: Unique name for this session
	 *  stream_mode: A value from CalicoModes
	 *
	 * Usage notes for Key parameter:
	 *
	 * 	WARNING: It is *essential* that the same key is never used twice, or
	 * 	else the security is broken.
	 *
	 * Usage notes for Stream Mode parameter:
	 *
	 * 	WARNING: It is *essential* that the initiator and responder use
	 * 	different modes when communicating, or else the security is broken.
	 *
	 * 	INITIATOR mode is for when this object is used by the client or the
	 * 	initiator of the session.
	 *
	 * 	RESPONDER mode is for when this object is used by the server or the
	 * 	responder of the session.
	 *
	 * Usage notes for Session Name parameter:
	 *
	 * 	WARNING: It is *essential* that each session name is unique per key
	 * 	or else the security is broken.
	 *
	 *	If multiple Calico objects are created from the same key, then each one
	 *	must have a different unique session name.  For instance, if you have
	 *	two sockets that both carry data encrypted with the same key, then each
	 *	one would need to call Initialize() with a different session name.
	 *
	 * Returns a CalicoResult value, ERR_GROOVY meaning success and any other
	 * value indicating an error.
	 *
	 * ERR_GROOVY = Success!
	 * ERR_BAD_INPUT = Parameters are invalid
	 */
	int Initialize(const void *key,				// Pointer to 32B key material
				   const char *session_name,	// Unique session name
				   int mode);					// Value from CalicoModes

	/*
	 * len = Encrypt(plaintext, plaintext_bytes, ciphertext, ciphertext_bytes)
	 *
	 * Encrypts the given data, adding 11 bytes of overhead
	 *
	 * So, max_bytes must be greater than data_bytes by the expected overhead,
	 * or else this function will fail with an error code of ERR_TOO_SMALL.
	 *
	 * When using this function, try to encrypt as much data at one time as
	 * possible.  Batching a lot of messages into a single encryption will
	 * improve throughput and lower bandwidth usage.
	 *
	 * WARNING: Will encrypt zero-length messages without complaint.
	 *
	 *  plaintext: Pointer to input plaintext
	 *  plaintext_bytes: Input buffer size
	 *  ciphertext: Pointer to output ciphertext
	 *  ciphertext_bytes: Output buffer size
	 *
	 * Returns the number of bytes of encrypted data,
	 * or returns a number less than zero to indicate an error.
	 *
	 * ERR_BAD_STATE = Initialize() must be called first
	 * ERR_BAD_INPUT = Parameters are invalid
	 * ERR_TOO_SMALL = Output buffer is too small to contain encrypted data
	 */
	int Encrypt(const void *plaintext,	// Pointer to input plaintext
				int plaintext_bytes,	// Input buffer size
				void *ciphertext,		// Pointer to output ciphertext
				int ciphertext_bytes);	// Output buffer size

	/*
	 * len = Decrypt(ciphertext, ciphertext_bytes)
	 *
	 * Decrypts the given ciphertext in-place, returning the plaintext size.
	 *
	 * WARNING: Will decrypt zero-length messages without complaint.
	 *
	 *  ciphertext: Pointer to ciphertext
	 *  ciphertext_bytes: Number of valid encrypted data bytes
	 *
	 * Returns the number of bytes of plaintext data,
	 * or returns a number less than zero to indicate an error.
	 *
	 * ERR_BAD_STATE = Initialize() must be called first
	 * ERR_TOO_SMALL = Too few data bytes to cover the overhead
	 * ERR_BAD_INPUT = Parameters are invalid
	 * ERR_IV_DROP = Message IV was not accepted (replayed or old)
	 * ERR_MAC_DROP = Message authentication check failed
	 */
	int Decrypt(void *ciphertext,		// Pointer to ciphertext
				int ciphertext_bytes);	// Number of valid encrypted data bytes
};


} // namespace calico

} // namespace cat

#endif // CAT_CALICO_HPP
