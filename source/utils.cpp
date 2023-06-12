#include "utils.hpp"
#include "download.hpp"
#include "extract.hpp"
#include <sstream>
#include <iomanip>
#include <filesystem>
#include <regex>
#include <ctime>
#include <iostream>

namespace i18n = brls::i18n;
using namespace i18n::literals;

namespace utils {

    std::string formatApplicationId(u64 ApplicationId)
    {
        std::stringstream strm;
        strm << std::uppercase << std::setfill('0') << std::setw(16) << std::hex << ApplicationId;
        return strm.str();
    }

    nlohmann::json getInstalledGames() {
        NsApplicationRecord* records = new NsApplicationRecord[64000]();
        uint64_t tid;
        NsApplicationControlData controlData;
        NacpLanguageEntry* langEntry = nullptr;
        
        Result rc;
        int recordCount = 0;
        size_t controlSize = 0;

        nlohmann::json games = nlohmann::json::array();

        rc = nsListApplicationRecord(records, 64000, 0, &recordCount);
        for (auto i = 0; i < recordCount; ++i) {
            tid = records[i].application_id;
            rc = nsGetApplicationControlData(NsApplicationControlSource_Storage, tid, &controlData, sizeof(controlData), &controlSize);
            if (R_FAILED(rc)) {
                continue; // Ou break je sais pas trop
            }
            rc = nacpGetLanguageEntry(&controlData.nacp, &langEntry);
            if (R_FAILED(rc)) {
                continue; // Ou break je sais pas trop
            }

            if (!langEntry->name) {
                continue;
            }

            std::string appName = langEntry->name;
            std::string titleId = formatApplicationId(tid);
            games.push_back({
                {"name", appName},
                {"tid", titleId}
            });
        }
        delete[] records;
        return games;
    }

    nlohmann::json searchGames(std::string gameTitle) {
        gameTitle = replaceSpacesWithPlus(gameTitle);

        const std::string api_url = fmt::format("https://gamebanana.com/apiv11/Util/Game/NameMatch?_sName={}", gameTitle);
        brls::Logger::debug("API URL: {}", api_url);

        nlohmann::json games = net::downloadRequest(api_url);

        return games;
    }

    std::string replaceSpacesWithPlus(const std::string& str) {
        std::string resultString = str;
        std::ranges::replace(resultString, ' ', '+');
        return resultString;
    }

    nlohmann::json getMods(const int& gameID, const int& page) {
        const std::string api_url = fmt::format("https://gamebanana.com/apiv11/Game/{}/Subfeed?_nPage={}?_nPerpage=50", std::to_string(gameID), std::to_string(page));
        nlohmann::json mods = net::downloadRequest(api_url);

        return mods;
    }

    nlohmann::json getMods(const int& gameID, const std::string&search, const int& page) {
        std::string search_term = replaceSpacesWithPlus(search);
        const std::string api_url = fmt::format("https://gamebanana.com/apiv11/Game/{}/Subfeed?_nPage={}&_nPerpage=50&_sName={}&_csvModelInclusions=Mod", std::to_string(gameID),std::to_string(page), search_term);
        nlohmann::json mods = net::downloadRequest(api_url);
        std::cout<< api_url << std::endl;
        std::cout << mods << std::endl;
        return mods;
    }

    nlohmann::json getDownloadLinks(const std::string& ModelName, const int& idRow) {
        const std::string api_url = fmt::format("https://gamebanana.com/apiv11/{}/{}?_csvProperties=@gbprofile", ModelName, idRow);
        nlohmann::json downloadLinks = net::downloadRequest(api_url);

        return downloadLinks.at("_aFiles");
    }

    uint8_t* getIconFromTitleId(const std::string& titleId) {
        if(titleId.empty()) return nullptr;

        uint8_t* icon = nullptr;
        NsApplicationControlData controlData;
        size_t controlSize  = 0;
        uint64_t tid;

        std::istringstream buffer(titleId);
        buffer >> std::hex >> tid;

        if (R_FAILED(nsGetApplicationControlData(NsApplicationControlSource_Storage, tid, &controlData, sizeof(controlData), &controlSize))){ return nullptr; }

        icon = new uint8_t[0x20000];
        memcpy(icon, controlData.icon, 0x20000);
        return icon;
    }

    std::vector<brls::Image*> getModsImages(const nlohmann::json& mod_json, const int& BigPage, size_t& sizeOfArray) {
        nlohmann::json preview_json = mod_json.at("_aPreviewMedia").at("_aImages");

        std::vector<brls::Image*> images;
        brls::Image* current_image;

        int compteur = 0;

        std::string url;

        for (auto i : preview_json) {
            std::vector<unsigned char> buffer;
            if (compteur == BigPage)
                url = fmt::format("https://images.gamebanana.com/img/ss/mods/{}", i.at("_sFile"));
            else 
                url = fmt::format("https://images.gamebanana.com/img/ss/mods/{}", i.at("_sFile100"));
            net::downloadImage(url, buffer);
            current_image = new brls::Image(buffer.data(), buffer.size());  

            if (compteur == BigPage)
                sizeOfArray = buffer.size();

            images.push_back(current_image);
            buffer.clear();  
            compteur++;         
            if (compteur > 5)
                break;
        
            svcOutputDebugString("0", 1);
        }
        return images;
    }

    void showDialogBoxInfo(const std::string& text)
    {
        brls::Dialog* dialog;
        dialog = new brls::Dialog(text);
        brls::GenericEvent::Callback callback = [dialog](brls::View* view) {
            dialog->close();
        };
        dialog->addButton("menus/common/ok"_i18n, callback);
        dialog->setCancelable(true);
        dialog->open();
    }

    int openWebBrowser(const std::string url)
    {
        Result rc = 0;
        int at = appletGetAppletType();
        if (at == AppletType_Application) {  // Running as a title
            WebCommonConfig conf;
            WebCommonReply out;
            rc = webPageCreate(&conf, url.c_str());
            if (R_FAILED(rc))
                return rc;
            webConfigSetJsExtension(&conf, true);
            webConfigSetPageCache(&conf, true);
            webConfigSetBootLoadingIcon(&conf, true);
            webConfigSetWhitelist(&conf, ".*");
            rc = webConfigShow(&conf, &out);
            if (R_FAILED(rc))
                return rc;
        }
        else {  // Running under applet
            showDialogBoxInfo("menus/utils/applet_webbrowser"_i18n);
        }
        return rc;
    }
    std::string removeHtmlTags(const std::string& input) {
        std::regex tagsRegex("<.*?>");
        return std::regex_replace(input, tagsRegex, "");
    }
    std::string getDescription(const int& mod_id) {
        const std::string api_url = fmt::format("https://gamebanana.com/apiv11/Mod/{}?_csvProperties=_sText", mod_id);
        nlohmann::json desc = net::downloadRequest(api_url);

        std::string rawText = desc.at("_sText");
        std::string cleanedText = removeHtmlTags(rawText);
        
        return cleanedText;
    }

    std::string convertTimestampToString(int timestamp) {
        std::time_t time = static_cast<std::time_t>(timestamp);
        std::tm* timeInfo = std::localtime(&time);

        std::ostringstream oss;
        oss << std::put_time(timeInfo, "%m/%d/%Y %H:%M:%S");

        return oss.str();
    }

    std::string formatFileSize(int size) {
        double fileSize = static_cast<double>(size);
        std::ostringstream oss;

        if (fileSize < 1024) {
            oss << fileSize << " octets";
        } else if (fileSize < 1048576) {
            oss << std::fixed << std::setprecision(2) << fileSize / 1024 << " Ko";
        } else if (fileSize < 1073741824) {
            oss << std::fixed << std::setprecision(2) << fileSize / 1048576 << " Mo";
        } else if (fileSize < 1099511627776) {
            oss << std::fixed << std::setprecision(2) << fileSize / 1073741824 << " Go";
        } else {
            oss << std::fixed << std::setprecision(2) << fileSize / 1099511627776 << " To";
        }

        return oss.str();
    }
}
