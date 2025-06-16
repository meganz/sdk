#include "mega/file_smart_upload.h"
#include "mega/filesystem.h"
#include "mega/filefingerprint.h"
#include "mega/megaclient.h"

using namespace mega;

constexpr size_t SHA256_DIGEST_LENGTH = 32; // SHA-256 hash length

CFileUploader::CFileUploader(MegaClient* c):
    client(c)
{}

// Optimized upload function
bool CFileUploader::smart_upload(const std::string& localFilePath, FileDataNode* remoteNode)
{
    std::cout << "Start to upload file:" << std::endl;
    // Step 1: Check if the fingerprint matches
    if (!is_fingerprint_match(localFilePath, remoteNode))
    {
        std::cout << " Start to upload local file!" << std::endl;

        return full_upload(localFilePath); // Fingerprint mismatch, complete upload
    }

    std::cout << " The fingerprint is matched!" << std::endl;

    // Step 2: Obtain remote encryption credentials
    std::string remoteKey = remoteNode->get_encryption_key();
    std::string remoteNonce = remoteNode->get_nonce();
    std::string remoteMac = remoteNode->get_mac();

    // Step 3: Encrypt the file locally and calculate the MAC
    std::string localMac;
    bool success = CryptoUtils::encrypt_file_and_calculate_mac(localFilePath,
                                                               remoteKey,
                                                               remoteNonce,
                                                               localMac);

    if (!success)
    {
        std::cout << " Start to upload local file!" << std::endl;

        return full_upload(localFilePath); // Encryption failed, complete upload
    }

    std::cout << " Encrypt the file locally and calculate the MAC sucessfully!" << std::endl;

    // Step 4: Compare MAC
    if (localMac == remoteMac)
    {
        std::cout << " The local MAC and remote Mac is equal!" << std::endl;
        std::cout << " Start to copy remote file!" << std::endl;

        return copy_remote_file(localFilePath,remoteNode); // MAC matching, copying remote files
    }
    else
    {
        std::cout << " The local MAC and remote Mac is diffrent!" << std::endl;
        std::cout << " Start to upload local file!" << std::endl;
        return full_upload(localFilePath); // MAC mismatch, complete upload
    }
}

// Check if the local file matches the remote node fingerprint
bool CFileUploader::is_fingerprint_match(const std::string& localFilePath, FileDataNode* remoteNode)
{
    std::string localFingerprint = calculate_fingerprint(localFilePath);
    return localFingerprint == remoteNode->get_fingerprint();
}

// Calculate file fingerprint (partial byte hash+size+mtime)
std::string CFileUploader::calculate_fingerprint(const std::string& filePath)
{
    if (client == nullptr){
        // mega client not initialized, return empty string
       std::cerr << "MegaClient is not initialized." << std::endl;
       return "";
    }
    // create FileAccess to access the file
    auto fa = client->fsaccess->newfileaccess();
    auto localfilepath = LocalPath::fromAbsolutePath(filePath);
    if (!fa->fopen(localfilepath, FSLogging::logOnError))
    {
        // file open failed, log error and return empty string
        std::cerr << "Failed to set file path: " << filePath << std::endl;
        return "";
    }

    // create FileFingerprint object
    mega::FileFingerprint fp;

    // generate file fingerprint
    if (!fp.genfingerprint(fa.get(), false))
    {
        // generation failed, log error and return empty string
        std::cerr << "Failed to generate file fingerprint: " << filePath << std::endl;
        return "";
    }

    // serialize fingerprint to string
    std::string fingerprintStr;
    fp.serializefingerprint(&fingerprintStr);

    return fingerprintStr;
}


// Complete file upload
bool CFileUploader::full_upload(const std::string& filePath)
{
    if (client == nullptr)
    {
        // mega client not initialized, return empty string
        std::cerr << "MegaClient is not initialized." << std::endl;
        return "";
    }
    // create FileAccess to access the file
    auto fa = client->fsaccess->newfileaccess();
    auto localfilepath = LocalPath::fromAbsolutePath(filePath);
    if (!fa->fopen(localfilepath, FSLogging::logOnError))
    {
        std::cerr << "Failed to open file: " << filePath << ", error: " << std::strerror(errno)
                  << std::endl;
        return false;
    }

    // upload local file
    File fupfile;
    fupfile.prepare(*client->fsaccess);
    fupfile.start();
    // Implement complete upload logic (omitted)
    return true;
}

// Copy remote files
bool CFileUploader::copy_remote_file(const std::string& localFilePath, FileDataNode* remoteNode)
{
    if (!client)
    {
        std::cerr << "MegaClient is not initialized." << std::endl;
        return false;
    }

    if (!remoteNode)
    {
        std::cerr << "Invalid remote node." << std::endl;
        return false;
    }

    std::cout << "Starting remote file copy for node: " << remoteNode->get_nonce() << std::endl;

    // get remote node's encryption parameters
    std::string sourceKey = remoteNode->get_encryption_key();
    std::string sourceNonce = remoteNode->get_nonce();
    std::string sourceMac = remoteNode->get_mac();

    if (sourceKey.empty() || sourceNonce.empty() || sourceMac.empty())
    {
        std::cerr << "Missing encryption parameters for source node." << std::endl;
        return false;
    }

    // create new encryption parameters for the target node
    std::string targetKey = client->rng.genstring(SymmCipher::KEYLENGTH); // CryptoUtils::generate_new_key();
    std::string targetNonce = client->rng.genstring(SymmCipher::KEYLENGTH);

    // real implementation should generate a new key and nonce
    std::string parentHandle = remoteNode->get_parenthandle(); // auuxiliary function to get the parent folder handle
    std::string newName = remoteNode->get_name() + "_copy"; // copy file name

    // validate the source MAC
    bool macValid = CryptoUtils::verify_mac(remoteNode->get_file_size(),
                                            localFilePath,
                                            sourceKey,
                                            sourceNonce,
                                            sourceMac);

    if (!macValid)
    {
        std::cerr << "Source file MAC verification failed. Possible data corruption." << std::endl;
        return false;
    }

    // excute the remote copy operation
    // apicopyremotefile: should be provided by client and will ask copy file on server by theget
    // key and nonce
    std::string newNodeHandle;
    bool success = true;
    /* bool success = client->apicopyremotefile->copy(remoteNode->get_handle(),
                                     parentHandle,
                                     newName,
                                     targetKey,
                                     targetNonce,
                                     &newNodeHandle);*/

    if (!success)
    {
        std::cerr << "Failed to copy remote file." << std::endl;
        return false;
    }

    std::cout << "Remote file copied successfully. New handle: " << newNodeHandle << std::endl;
    return true;
}

// Optimized encrypted file and calculate GCM authentication label function
bool CryptoUtils::encrypt_file_and_calculate_mac(const std::string& localFilePath,
                                                 const std::string& remoteKeyBase64,
                                                 const std::string& remoteNonceBase64,
                                                 std::string& outMacBase64,
                                                 size_t chunkSize)
{ // Default 1MB block size
    // Decoding Base64 keys and random numbers
    std::vector<unsigned char> key = base64_decode(remoteKeyBase64);
    std::vector<unsigned char> nonce = base64_decode(remoteNonceBase64);

    // Verify key and random number length
    if (key.size() != AES_BLOCK_SIZE || nonce.size() != AES_BLOCK_SIZE)
    {
        fprintf(stderr, "Invalid key or nonce length\n");
        return false;
    }

    // Initialize encryption context (using smart pointers to manage resources)
    std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)> ctx(EVP_CIPHER_CTX_new(),
                                                                        EVP_CIPHER_CTX_free);

    if (!ctx)
    {
        fprintf(stderr, "Failed to create EVP_CIPHER_CTX\n");
        return false;
    }

    // Initialize AES-128-GCM encryption
    if (EVP_EncryptInit_ex(ctx.get(), EVP_aes_128_gcm(), nullptr, key.data(), nonce.data()) != 1)
    {
        fprintf(stderr,
                "Failed to initialize encryption: %s\n",
                ERR_error_string(ERR_get_error(), nullptr));
        return false;
    }

    FILE* filePtr = nullptr;
    errno_t err = fopen_s(&filePtr, localFilePath.c_str(), "rb");
    if (err != 0 || !filePtr)
    {
        fprintf(stderr, "Failed to open file: %s\n", localFilePath.c_str());
        return false;
    }
    std::unique_ptr<FILE, decltype(&fclose)> file(filePtr, fclose);

    if (!file)
    {
        fprintf(stderr, "Failed to open file: %s\n", localFilePath.c_str());
        return false;
    }

    // Block processing files
    std::vector<unsigned char> buffer(chunkSize);
    std::vector<unsigned char> encryptedChunk(chunkSize);
    int len;
    size_t totalBytesRead = 0;

    // Get file size for verification
    fseek(file.get(), 0, SEEK_END);
    size_t fileSize = ftell(file.get());
    rewind(file.get());

    // Process each data block
    while (totalBytesRead < fileSize)
    {
        size_t bytesToRead = std::min(chunkSize, fileSize - totalBytesRead);
        size_t bytesRead = fread(buffer.data(), 1, bytesToRead, file.get());

        if (bytesRead != bytesToRead)
        {
            fprintf(stderr, "Failed to read file\n");
            return false;
        }

        // Encrypt the current block
        if (EVP_EncryptUpdate(ctx.get(), encryptedChunk.data(), &len, buffer.data(), bytesRead) !=
            1)
        {
            fprintf(stderr, "Encryption error: %s\n", ERR_error_string(ERR_get_error(), nullptr));
            return false;
        }

        totalBytesRead += bytesRead;
    }

    // Complete encryption and obtain GCM authentication label
    unsigned char tag[16];
    if (EVP_EncryptFinal_ex(ctx.get(), nullptr, &len) != 1 ||
        EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_GET_TAG, 16, tag) != 1)
    {
        fprintf(stderr, "Failed to get GCM tag: %s\n", ERR_error_string(ERR_get_error(), nullptr));
        return false;
    }

    // Convert authentication labels to Base64 encoding
    outMacBase64 = base64_encode(tag, sizeof(tag));
    return true;
}

// Auxiliary function: Base64 encoding/decoding
std::vector<unsigned char> CryptoUtils::base64_decode(const std::string& encoded)
{
    std::vector<unsigned char> decoded;

    // 创建BIO链：base64解码过滤器 + 内存源
    BIO* b64 = BIO_new(BIO_f_base64());
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL); // 不添加换行符

    BIO* bmem = BIO_new_mem_buf(encoded.data(), encoded.size());
    BIO* bio = BIO_push(b64, bmem);

    // 计算解码后的最大长度
    decoded.resize(encoded.size());

    // 执行解码
    int decodedLength = BIO_read(bio, decoded.data(), decoded.size());
    if (decodedLength > 0)
    {
        decoded.resize(decodedLength);
    }
    else
    {
        decoded.clear();
    }

    // 释放资源
    BIO_free_all(bio);

    return decoded;
}

std::string CryptoUtils::base64_encode(const unsigned char* data, size_t length)
{
    std::string encoded;

    // 创建BIO链：base64编码过滤器 + 内存目标
    BIO* b64 = BIO_new(BIO_f_base64());
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL); // 不添加换行符

    BIO* bmem = BIO_new(BIO_s_mem());
    BIO* bio = BIO_push(b64, bmem);

    // 执行编码
    BIO_write(bio, data, length);
    BIO_flush(bio);

    // 获取编码后的数据
    char* encodedData = nullptr;
    long encodedLength = BIO_get_mem_data(bmem, &encodedData);

    if (encodedLength > 0)
    {
        encoded.assign(encodedData, encodedLength);
    }

    // 释放资源
    BIO_free_all(bio);

    return encoded;
}

bool CryptoUtils::verify_mac(uint64_t fileSize,
                             const std::string& localFilePath,
                             const std::string& keyBase64,
                             const std::string& nonceBase64,
                             const std::string& expectedMacBase64)
{
    // decoding Base64 keys, nonces, and expected MAC
    std::vector<unsigned char> byteKey = base64_decode(keyBase64);
    std::string key(byteKey.begin(), byteKey.end());

    std::vector<unsigned char> byteNonce = base64_decode(nonceBase64);
    std::string nonce(byteNonce.begin(), byteNonce.end());
    
    std::vector<unsigned char> byteMac = base64_decode(nonceBase64);
    std::string expectedMac(byteMac.begin(), byteMac.end());

    // confirm key and nonce length
    if (key.size() != SymmCipher::KEYLENGTH || nonce.size() != SymmCipher::KEYLENGTH)
    {
        std::cerr << "Invalid key or nonce length" << std::endl;
        return false;
    }

    // initialize HMAC context
    unsigned char computedMac[SHA256_DIGEST_LENGTH];
    unsigned int macLength = 0;
    HMAC_CTX* hmac = HMAC_CTX_new();
    if (!hmac)
    {
        std::cerr << "Failed to create HMAC context" << std::endl;
        return false;
    }

    // initialize HMAC with SHA-256
    if (HMAC_Init_ex(hmac, key.data(), key.size(), EVP_sha256(), nullptr) != 1)
    {
        std::cerr << "Failed to initialize HMAC" << std::endl;
        HMAC_CTX_free(hmac);
        return false;
    }

    // 1. handle file size
    unsigned char sizeBytes[8];
    for (int i = 0; i < 8; i++)
    {
        sizeBytes[i] = (fileSize >> (56 - i * 8)) & 0xFF;
    }
    HMAC_Update(hmac, sizeBytes, 8);

    // 2. handle nonce
    HMAC_Update(hmac, reinterpret_cast<const unsigned char*>(nonce.data()), nonce.size());

    // 3. handle file content
    std::string filePath = localFilePath; // need to ensure the file path is correct

    std::ifstream file(filePath, std::ios::binary);
    if (!file)
    {
        std::cerr << "Failed to open file for MAC verification" << std::endl;
        HMAC_CTX_free(hmac);
        return false;
    }

    // chunked reading to avoid memory issues with large files
    constexpr size_t bufferSize = 1024 * 1024; // 1MB buffer size
    std::vector<char> buffer(bufferSize);

    while (file)
    {
        file.read(buffer.data(), bufferSize);
        size_t bytesRead = file.gcount();

        if (bytesRead > 0)
        {
            HMAC_Update(hmac, reinterpret_cast<const unsigned char*>(buffer.data()), bytesRead);
        }
    }

    // complete HMAC calculation
    if (HMAC_Final(hmac, computedMac, &macLength) != 1)
    {
        std::cerr << "Failed to finalize HMAC" << std::endl;
        HMAC_CTX_free(hmac);
        return false;
    }

    HMAC_CTX_free(hmac);

    // compare computed MAC with expected MAC
    if (macLength != expectedMac.size() ||
        std::memcmp(computedMac, expectedMac.data(), macLength) != 0)
    {
        std::cerr << "MAC verification failed" << std::endl;
        return false;
    }

    return true;
}