#ifndef ANALYTIC_SERVER_PROJ_UTIL_H
#define ANALYTIC_SERVER_PROJ_UTIL_H

#include <string>
#include <spdlog/logger.h>
#include "common.h"
#include "json.hpp"

using namespace std;
using json = nlohmann::json;

namespace nmetics {

    class Util {
    public:
        static std::string executablePath();
        static std::string getLogsFolderPath();
        static std::string getConfigFilePath();
        static std::shared_ptr<spdlog::logger> createAsyncLoggerAndRegister(const std::string& mLoggerName, const std::string& logDirPath,
                const std::string& logFilePrefix, bool createConsoleSink=false, spdlog::level::level_enum log_level = spdlog::level::level_enum::info);
        static bool createDirectories(const std::string& path);

        static const std::string HexDecodeString(const std::string &string_to_decode);
        static bool VerifySignature(const std::string &data, const std::string &message);

    };
}

#endif
