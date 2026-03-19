#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace mil {

struct HttpResponse {
    long statusCode = 0;
    std::string body;
};

bool HttpGetToString(const std::string& url, HttpResponse& response, std::string& error);
bool HttpDownloadToFile(const std::string& url, const std::string& outputPath, std::size_t* downloadedBytes, std::string& error);
bool HttpGetWithCache(const std::vector<std::string>& urls,
                      const std::string& cachePath,
                      HttpResponse& response,
                      std::string& source,
                      std::string& error);

}  // namespace mil
