/**
 * @file mega/file_uploader.h
 * @brief incrment action  packet precessing
 *
 * (c) 2025-2025 by Mega Limited, Wellsford, New Zealand
 *
 * This file is part of the MEGA SDK - Client Access Engine.
 *
 * Applications using the MEGA API must present a valid application key
 * and comply with the the rules set forth in the Terms of Service.
 *
 * The MEGA SDK is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * @copyright Simplified (2-clause) BSD License.
 *
 * You should have received a copy of the license along with this
 * program.
 */
#pragma once
#include "types.h"
#include <openssl/aes.h>
#include <openssl/evp.h>
#include <openssl/err.h>
#include <openssl/hmac.h>
#include <openssl/bio.h>

namespace mega {

	class FileDataNode {
	public:
		// Basic property
		uint64_t id;
		std::string name;
		uint64_t size;
		time_t mtime;
		std::string fingerprint;
        std::string parenthandle;

		// Add encryption related attributes
		std::string encryption_key;
		std::string nonce; 
		std::string mac;
	public:
		// Getter
		std::string get_encryption_key() const { return encryption_key; }
		std::string get_nonce() const { return nonce; }
		std::string get_mac() const { return mac; }
		std::string get_fingerprint() const { return fingerprint; }
		std::string get_name() const { return name; }

        uint64_t get_file_size() const{ return size;}
		uint64_t get_handle() const { return id; }
		std::string get_parenthandle() const { return parenthandle;}
	};

	class CFileUploader {
	public:
        // constructor
        CFileUploader(MegaClient* c);

		// Optimized upload function
        bool smart_upload(const std::string& localFilePath, FileDataNode* remoteNode);

	private:
		// Check if the local file matches the remote node fingerprint
        bool is_fingerprint_match(const std::string& localFilePath, FileDataNode* remoteNode);

		// Calculate file fingerprint (partial byte hash+size+mtime)
		std::string calculate_fingerprint(const std::string& filePath);

		// Complete file upload
		bool full_upload(const std::string& filePath);

		// Copy remote file
        bool copy_remote_file(const std::string& localFilePath,FileDataNode* remoteNode);

	private:
        // client pointer to interact with the Mega API
        MegaClient* client = nullptr;
	};

	// Encryption tools class
	class CryptoUtils {
	public:
		// //Encrypt local files using remote encryption credentials and calculate MAC
		static bool encrypt_file_and_calculate_mac(
			const std::string& localFilePath,
			const std::string& remoteKeyBase64,
			const std::string& remoteNonceBase64,
			std::string& outMacBase64,
			size_t chunkSize = 1024 * 1024);
		// Base64 encoding and decoding auxiliary functions

		static bool verify_mac(uint64_t fileSize,
                        const std::string& localFilePath,
                        const std::string& keyBase64,
                        const std::string& nonceBase64,
                        const std::string& expectedMacBase64);
	private:
		static std::vector<unsigned char> base64_decode(const std::string& encoded);
		static std::string base64_encode(const unsigned char* data, size_t length);
	};
} // namespace mega
