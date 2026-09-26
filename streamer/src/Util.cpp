#include "Util.h"
#include <random>
#include <spdlog/sinks/daily_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/async_logger.h>
#include <spdlog/async.h>
#include <iostream>
#include <boost/filesystem.hpp>
#include <boost/dll.hpp>
#include <json.hpp>
#include "base64.h"

#include <cryptopp/aes.h>
#include <cryptopp/secblock.h>
#include <cryptopp/osrng.h>
#include <cryptopp/modes.h>
#include <cryptopp/rsa.h>
#include <cryptopp/sha.h>
#include <cryptopp/hex.h>
#include <cryptopp/filters.h>

#include <spdlog/spdlog.h>
#include <spdlog/async.h>

namespace fs = boost::filesystem;
using namespace nmetics;

std::string Util::executablePath()
{
    return boost::dll::program_location().parent_path().string();
}

std::string Util::getLogsFolderPath()
{
    static const std::string logsFolder =  fmt::format("{0}/{1}", Util::executablePath(), nmetics::LOG_FOLDER_NAME);
    return logsFolder;
}

std::string Util::getConfigFilePath()
{
    static const std::string mainConfigFile = fmt::format("{0}/{1}", executablePath(), nmetics::CONFIG_FILE_NAME);
    return mainConfigFile;
}


std::shared_ptr<spdlog::logger> Util::createAsyncLoggerAndRegister(const std::string& mLoggerName, const std::string& log_dir_path,
        const std::string& logFilePrefix, bool createConsoleSink, spdlog::level::level_enum log_level)
{
    try {

        auto mLogger = spdlog::get(mLoggerName);

        if (not mLogger)
        {
            std::string logFileName = fmt::format("{0}/{1}", log_dir_path, logFilePrefix);
            std::vector<spdlog::sink_ptr> sinks;

            if (createConsoleSink) {
#if _WIN32
                sinks.push_back(std::make_shared<spdlog::sinks::wincolor_stdout_sink_mt>()); // on windows use windows sink
#else
                sinks.push_back(std::make_shared<spdlog::sinks::ansicolor_stdout_sink_mt>());
#endif
            }

            sinks.push_back(std::make_shared<spdlog::sinks::daily_file_sink_mt>(logFileName, 23, 59));
            mLogger = std::make_shared<spdlog::async_logger>(mLoggerName, begin(sinks), end(sinks), spdlog::thread_pool());
            mLogger->set_level(log_level);
            mLogger->flush_on(spdlog::level::trace);
            spdlog::register_logger(mLogger);
        }
        return mLogger;
    }
    catch (std::exception& ex)
    {
        std::cout << "[Logger] Failed to initialize logger: name=" << mLoggerName << ", error=" << ex.what() << std::endl;
    }
    return nullptr;
}

bool Util::createDirectories(const std::string& path)
{
    try {
        if (not fs::exists(path)) {
            fs::create_directories(path);
        }
        return true;
    }
    catch (const std::exception& ex) {}
    return false;
}

const std::string Util::HexDecodeString(const std::string &string_to_decode) {

    std::string hex_decoded_string;
    CryptoPP::StringSource ss3(string_to_decode, true, new CryptoPP::HexDecoder(new CryptoPP::StringSink(hex_decoded_string)));

    return hex_decoded_string;
}

bool Util::VerifySignature(const std::string &data, const std::string &message) {

    const char* k = "30820222300D06092A864886F70D01010105000382020F003082020A0282020100CA6F4348BDD0963790AB94843252A34B66A6F2A8BCEE76429AD6C5F134F6779891607C2A4391BAC5E7A55B0C54D1B39D757BC7FB0C42278967A57B978F64A9748C01827B59B604D872CD0D39066AF5EFEFE8111482742AA8029E5B76449D90CA017E9542C35488CA32FFBAD8E6444DAD97E1C787ADE386FE78915D90B2D4199EAB7BDC7186F345DCC8E7929D5287D4EA536B31C8B0B326BC4330B44FDC0CF41BFA368BF4D8480A4C66E8474D0B2EFEE024B7E96332B3470EF784B37D052600A1C28237E7AE1FFA69E94EE037F282896E919F32171927BE07E83DC0A5D745997D2E698F2873BD615FA0722EE33EECE9E69187FC15956C176F6C35C541BF8FDFCB8405A5EB2549F3BBF63B547FD916219F0476A62745A192506C9AE810F0D1BD14675D08206BE4B22920F1BA1677F7E6626F1251A1486E437B2CEC82DE8992C6968574F2FC825D4609D8982D78B0D36E60E5E672C7C0A26F424ED882C1BB23DFA3CDDEF182D9BF7D3C0C850658C48D9E53447472322F6B98FAF4DEF396EDB83CCFD0A8E105F7D8ACEC69F9F02D8B75AB942E89EC58D5B9A187A9227E47B0D6C33C03E3A66779D7E090B2EE2B688964971A11C6CC5717FE2AEE60F8614C5F984F2CCBDE63B4490EB064AE6713078D7FD0D5F47427D7683F71549E95899128CAE16FB69823E5B3CFA044F3FCBB2B17553F38F79C5B2B470D69C6FEF17C55CEACD9650203010001";

    try {
        std::string hex_decoded_data = HexDecodeString(data);

        nlohmann::json  json_data = nlohmann::json::parse(hex_decoded_data);

#if _LICENSING_DEBUG
        std::cout << json_data.dump(4) << std::endl;
#endif
        auto license_data = json_data.at("Data").get<std::string>();
        if (license_data != message) {
            return false;
        }
        auto signature_base64 = json_data.at("Signature").get<std::string>();

        std::string signature = base64_decode(signature_base64);

        // load public key
        CryptoPP::RSA::PublicKey publicKey;
        publicKey.Load(CryptoPP::StringSource(k, true, new CryptoPP::HexDecoder()).Ref());

        // validate key
        CryptoPP::AutoSeededRandomPool rnd;
        if (!publicKey.Validate(rnd, 3)) {
            //std::cout << "Key validation failed" << std::endl;
            /*result.Error = LicenseErrorType::PublicKeyLoadError;
            result.Message = "Unable to load validate key";*/
            return false;
        }

        // verify signature
        //CryptoPP::RSASS<CryptoPP::PKCS1v15, CryptoPP::SHA1>::Verifier verifier(publicKey);
        CryptoPP::RSASSA_PKCS1v15_SHA_Verifier verifier(publicKey);

        bool signature_matched = verifier.VerifyMessage(reinterpret_cast<const CryptoPP::byte*>(license_data.c_str()), license_data.length(),
                                                        reinterpret_cast<const CryptoPP::byte*>(signature.c_str()), signature.length());

        if (!signature_matched) {
            /*result.Error = LicenseErrorType::LicenseSignatureMismatch;
            result.Message = "Signature does not match";*/
        }

        return signature_matched;
    }
    catch (const std::exception & ex) {
        /*result.Error = LicenseErrorType::Exception;
        result.Message = ex.what();*/
        std::cout << fmt::format("Error in VerifySignature: {}", ex.what()) << std::endl;
    }
    return false;
}
