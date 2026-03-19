#pragma once

#include <string>
#include <vector>

#include "mil/models.hpp"

namespace mil {

std::vector<InstallReceipt> LoadInstallReceipts(std::string& note);
bool InstallPackage(const CatalogEntry& entry, const InstalledTitle* installedTitle, InstallReceipt& receipt, std::string& error);
bool UninstallPackage(const InstallReceipt& receipt, std::string& error);
bool FindReceiptForPackage(const std::vector<InstallReceipt>& receipts, const std::string& packageId, InstallReceipt* receipt);

}  // namespace mil
