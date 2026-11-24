#include "PersistenceManager.h"

#include <cctype>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <windows.h>

namespace
{
    std::string jsonEscape(const std::string& s)
    {
        std::string out;
        out.reserve(s.size());
        for (char c : s) {
            if      (c == '"')  out += "\\\"";
            else if (c == '\\') out += "\\\\";
            else if (c == '\n') out += "\\n";
            else if (c == '\r') out += "\\r";
            else                out += c;
        }
        return out;
    }

    bool extractStr(const std::string& json, const std::string& key, std::string& out)
    {
        const std::string needle = "\"" + key + "\"";
        const std::size_t keyPos = json.find(needle);
        if (keyPos == std::string::npos) return false;

        const std::size_t colon = json.find(':', keyPos + needle.size());
        if (colon == std::string::npos) return false;

        std::size_t valStart = colon + 1;
        while (valStart < json.size() && (json[valStart] == ' ' || json[valStart] == '\t'))
            ++valStart;

        if (valStart >= json.size() || json[valStart] != '"') return false;
        ++valStart;

        const std::size_t valEnd = json.find('"', valStart);
        if (valEnd == std::string::npos) return false;

        out = json.substr(valStart, valEnd - valStart);
        return true;
    }

    bool extractInt(const std::string& json, const std::string& key, int64_t& out)
    {
        const std::string needle = "\"" + key + "\"";
        const std::size_t keyPos = json.find(needle);
        if (keyPos == std::string::npos) return false;

        const std::size_t colon = json.find(':', keyPos + needle.size());
        if (colon == std::string::npos) return false;

        std::size_t valStart = colon + 1;
        while (valStart < json.size() && (json[valStart] == ' ' || json[valStart] == '\t'))
            ++valStart;

        std::size_t numEnd = valStart;
        if (numEnd < json.size() && json[numEnd] == '-') ++numEnd;
        while (numEnd < json.size() && std::isdigit(static_cast<unsigned char>(json[numEnd])))
            ++numEnd;

        if (numEnd == valStart) return false;

        try {
            out = static_cast<int64_t>(std::stoll(json.substr(valStart, numEnd - valStart)));
            return true;
        } catch (...) {
            return false;
        }
    }

    std::size_t findMatchingBracket(const std::string& str, std::size_t openPos)
    {
        int depth = 0;
        bool inString = false;
        for (std::size_t i = openPos; i < str.size(); ++i)
        {
            if (str[i] == '"' && (i == 0 || str[i - 1] != '\\'))
                inString = !inString;
            if (!inString)
            {
                if (str[i] == '[') depth++;
                else if (str[i] == ']')
                {
                    depth--;
                    if (depth == 0) return i;
                }
            }
        }
        return std::string::npos;
    }

    std::size_t findMatchingBrace(const std::string& str, std::size_t openPos)
    {
        int depth = 0;
        bool inString = false;
        for (std::size_t i = openPos; i < str.size(); ++i)
        {
            if (str[i] == '"' && (i == 0 || str[i - 1] != '\\'))
                inString = !inString;
            if (!inString)
            {
                if (str[i] == '{') depth++;
                else if (str[i] == '}')
                {
                    depth--;
                    if (depth == 0) return i;
                }
            }
        }
        return std::string::npos;
    }

    void ensureDirectory(const std::string& filePath)
    {
        const std::size_t slash = filePath.find_last_of("\\/");
        if (slash != std::string::npos)
        {
            const std::string dir = filePath.substr(0, slash);
            CreateDirectoryA(dir.c_str(), NULL);
        }
    }
}

bool PersistenceManager::saveState(const std::string& filePath,
                                   const PaymentStore& store,
                                   const Ledger& ledger)
{
    ensureDirectory(filePath);

    const std::string tmpPath = filePath + ".tmp";
    std::ofstream out(tmpPath, std::ios::binary);
    if (!out)
    {
        std::cerr << "[PERSISTENCE] Failed to open temp file for writing: " << tmpPath << "\n";
        return false;
    }

    const auto payments = store.all();
    const auto entries  = ledger.getAllEntries();

    out << "{\n";
    out << "  \"payments\": [\n";
    for (std::size_t i = 0; i < payments.size(); ++i)
    {
        const auto& p = payments[i];
        out << "    {\n";
        out << "      \"id\": \""              << jsonEscape(p.id)             << "\",\n";
        out << "      \"idempotency_key\": \"" << jsonEscape(p.idempotencyKey) << "\",\n";
        out << "      \"amount\": "            << p.amount                     << ",\n";
        out << "      \"currency\": \""        << jsonEscape(p.currency)       << "\",\n";
        out << "      \"description\": \""     << jsonEscape(p.description)    << "\",\n";
        out << "      \"status\": \""          << statusToString(p.status)     << "\",\n";
        out << "      \"created_at\": \""      << jsonEscape(p.createdAt)      << "\",\n";
        out << "      \"updated_at\": \""      << jsonEscape(p.updatedAt)      << "\",\n";
        out << "      \"history\": [";
        for (std::size_t h = 0; h < p.history.size(); ++h) {
            if (h > 0) out << ", ";
            out << "\"" << jsonEscape(p.history[h]) << "\"";
        }
        out << "]\n";
        out << "    }" << (i + 1 < payments.size() ? "," : "") << "\n";
    }
    out << "  ],\n";

    out << "  \"ledger_entries\": [\n";
    for (std::size_t i = 0; i < entries.size(); ++i)
    {
        const auto& e = entries[i];
        out << "    {\n";
        out << "      \"id\": \""             << jsonEscape(e.id)            << "\",\n";
        out << "      \"transaction_id\": \"" << jsonEscape(e.transactionId) << "\",\n";
        out << "      \"account_id\": \""     << jsonEscape(e.accountId)     << "\",\n";
        out << "      \"type\": \""           << entryTypeToString(e.type)   << "\",\n";
        out << "      \"amount\": "           << e.amount                    << ",\n";
        out << "      \"currency\": \""       << jsonEscape(e.currency)      << "\",\n";
        out << "      \"description\": \""    << jsonEscape(e.description)   << "\",\n";
        out << "      \"created_at\": \""     << jsonEscape(e.createdAt)     << "\"\n";
        out << "    }" << (i + 1 < entries.size() ? "," : "") << "\n";
    }
    out << "  ]\n";
    out << "}\n";

    out.flush();
    out.close();

    // Atomic file replacement: swap temp file to final destination
    if (MoveFileExA(tmpPath.c_str(), filePath.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == 0)
    {
        // Fallback standard rename if MoveFileEx failed
        std::remove(filePath.c_str());
        if (std::rename(tmpPath.c_str(), filePath.c_str()) != 0)
        {
            std::cerr << "[PERSISTENCE] Atomic swap failed to replace " << filePath << "\n";
            return false;
        }
    }

    return true;
}

bool PersistenceManager::loadState(const std::string& filePath,
                                   PaymentStore& store,
                                   Ledger& ledger)
{
    std::ifstream in(filePath, std::ios::binary);
    if (!in)
    {
        // First boot or no prior state file
        return true;
    }

    std::string content((std::istreambuf_iterator<char>(in)),
                        std::istreambuf_iterator<char>());
    in.close();

    if (content.empty())
        return true;

    std::vector<Payment> loadedPayments;
    std::vector<LedgerEntry> loadedEntries;

    // Parse payments array
    std::size_t paymentsArrayStart = content.find("\"payments\"");
    if (paymentsArrayStart != std::string::npos)
    {
        std::size_t pOpen = content.find('[', paymentsArrayStart);
        if (pOpen != std::string::npos)
        {
            std::size_t pClose = findMatchingBracket(content, pOpen);
            if (pClose != std::string::npos)
            {
                std::size_t objStart = pOpen;
                while ((objStart = content.find('{', objStart)) != std::string::npos && objStart < pClose)
                {
                    std::size_t objEnd = findMatchingBrace(content, objStart);
                    if (objEnd == std::string::npos || objEnd > pClose) break;

                    const std::string pBlock = content.substr(objStart, objEnd - objStart + 1);

                    Payment p;
                    extractStr(pBlock, "id", p.id);
                    extractStr(pBlock, "idempotency_key", p.idempotencyKey);
                    extractInt(pBlock, "amount", p.amount);
                    extractStr(pBlock, "currency", p.currency);
                    extractStr(pBlock, "description", p.description);
                    std::string stStr;
                    if (extractStr(pBlock, "status", stStr))
                        p.status = stringToStatus(stStr);
                    extractStr(pBlock, "created_at", p.createdAt);
                    extractStr(pBlock, "updated_at", p.updatedAt);

                    // Parse history array
                    std::size_t histPos = pBlock.find("\"history\"");
                    if (histPos != std::string::npos)
                    {
                        std::size_t hOpen = pBlock.find('[', histPos);
                        if (hOpen != std::string::npos)
                        {
                            std::size_t hClose = findMatchingBracket(pBlock, hOpen);
                            if (hClose != std::string::npos)
                            {
                                std::size_t sQuote = hOpen;
                                while ((sQuote = pBlock.find('"', sQuote)) != std::string::npos && sQuote < hClose)
                                {
                                    std::size_t eQuote = pBlock.find('"', sQuote + 1);
                                    if (eQuote == std::string::npos || eQuote > hClose) break;
                                    p.history.push_back(pBlock.substr(sQuote + 1, eQuote - sQuote - 1));
                                    sQuote = eQuote + 1;
                                }
                            }
                        }
                    }

                    if (!p.id.empty())
                        loadedPayments.push_back(p);

                    objStart = objEnd + 1;
                }
            }
        }
    }

    // Parse ledger_entries array
    std::size_t ledgerArrayStart = content.find("\"ledger_entries\"");
    if (ledgerArrayStart != std::string::npos)
    {
        std::size_t lOpen = content.find('[', ledgerArrayStart);
        if (lOpen != std::string::npos)
        {
            std::size_t lClose = findMatchingBracket(content, lOpen);
            if (lClose != std::string::npos)
            {
                std::size_t objStart = lOpen;
                while ((objStart = content.find('{', objStart)) != std::string::npos && objStart < lClose)
                {
                    std::size_t objEnd = findMatchingBrace(content, objStart);
                    if (objEnd == std::string::npos || objEnd > lClose) break;

                    const std::string lBlock = content.substr(objStart, objEnd - objStart + 1);

                    LedgerEntry e;
                    extractStr(lBlock, "id", e.id);
                    extractStr(lBlock, "transaction_id", e.transactionId);
                    extractStr(lBlock, "account_id", e.accountId);
                    std::string typeStr;
                    if (extractStr(lBlock, "type", typeStr))
                        e.type = (typeStr == "DEBIT") ? EntryType::DEBIT : EntryType::CREDIT;
                    extractInt(lBlock, "amount", e.amount);
                    extractStr(lBlock, "currency", e.currency);
                    extractStr(lBlock, "description", e.description);
                    extractStr(lBlock, "created_at", e.createdAt);

                    if (!e.id.empty())
                        loadedEntries.push_back(e);

                    objStart = objEnd + 1;
                }
            }
        }
    }

    store.loadFrom(loadedPayments);
    ledger.loadFrom(loadedEntries);

    std::cout << "[PERSISTENCE] Successfully hydrated " << loadedPayments.size()
              << " payments and " << loadedEntries.size() << " ledger entries from "
              << filePath << "\n";

    return true;
}
