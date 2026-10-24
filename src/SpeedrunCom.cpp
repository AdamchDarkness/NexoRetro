#include "SpeedrunCom.hpp"

#include "Json.hpp"
#include "net/Http.hpp"

#include <SDL3/SDL.h>

#include <cmath>
#include <cstdlib>

namespace {

constexpr const char* kApi = "https://www.speedrun.com/api/v1/";
constexpr size_t kMaxCategories = 10; // (une requête par catégorie pour son record)

bool getJson(net::HttpClient& http, const std::string& url, json::Value& out, std::string& error)
{
    const net::HttpResponse response = http.get(url);
    if (!response.ok()) {
        error = response.status == 0 ? "speedrun.com injoignable (connexion Internet ?)"
                                     : "speedrun.com a répondu " + std::to_string(response.status);
        return false;
    }
    if (!json::Parser(response.body).parse(out) || out.type != json::Value::Type::Object) {
        error = "Réponse de speedrun.com illisible";
        return false;
    }
    return true;
}

// Le tableau "data" d'une réponse de l'API (nullptr s'il manque).
const json::Value* dataArray(const json::Value& root)
{
    const json::Value* data = root.get("data");
    return data && data->type == json::Value::Type::Array ? data : nullptr;
}

} // namespace

SpeedrunCom::~SpeedrunCom()
{
    if (m_thread.joinable())
        m_thread.join();
}

void SpeedrunCom::search(const std::string& gameTitle)
{
    if (m_busy)
        return;
    if (m_thread.joinable())
        m_thread.join();
    m_busy = true;
    m_thread = std::thread([this, gameTitle] { run(gameTitle); });
}

std::optional<SpeedrunCom::Result> SpeedrunCom::take()
{
    std::lock_guard lock(m_mutex);
    std::optional<Result> r;
    r.swap(m_result);
    return r;
}

void SpeedrunCom::run(std::string title)
{
    Result result;
    net::HttpClient http("NexoRetro (speedrun)");
    // 1. Le jeu, d'après son nom.
    json::Value games;
    if (getJson(http, std::string(kApi) + "games?max=1&name=" + net::urlEncode(title), games, result.error)) {
        const json::Value* list = dataArray(games);
        if (!list || list->items.empty()) {
            result.error = "« " + title + " » est introuvable sur speedrun.com";
        } else {
            const json::Value& game = list->items.front();
            const std::string id = game.string("id");
            if (const json::Value* names = game.get("names"))
                result.gameName = names->string("international");
            result.gameLink = game.string("weblink");
            // 2. Ses catégories (celles du jeu entier, pas celles d'un niveau seul).
            json::Value cats;
            if (getJson(http, std::string(kApi) + "games/" + id + "/categories", cats, result.error))
                if (const json::Value* catList = dataArray(cats))
                    for (const json::Value& c : catList->items) {
                        if (c.string("type") != "per-game" || result.categories.size() >= kMaxCategories)
                            continue;
                        OfficialCategory oc;
                        oc.name = c.string("name");
                        oc.weblink = c.string("weblink");
                        // 3. Le record du monde de la catégorie, et son auteur.
                        json::Value records;
                        std::string ignored;
                        if (getJson(http, std::string(kApi) + "categories/" + c.string("id") + "/records?top=1&embed=players",
                                    records, ignored))
                            if (const json::Value* recList = dataArray(records); recList && !recList->items.empty()) {
                                const json::Value& board = recList->items.front();
                                const json::Value* runs = board.get("runs");
                                if (runs && !runs->items.empty())
                                    if (const json::Value* run = runs->items.front().get("run"))
                                        if (const json::Value* times = run->get("times"))
                                            if (const json::Value* t = times->get("primary_t"))
                                                oc.wrMs = std::llround(std::atof(t->text.c_str()) * 1000.0);
                                if (const json::Value* players = board.get("players"))
                                    if (const json::Value* pdata = players->get("data"); pdata && !pdata->items.empty()) {
                                        const json::Value& p = pdata->items.front();
                                        if (const json::Value* names = p.get("names"))
                                            oc.wrBy = names->string("international");
                                        if (oc.wrBy.empty())
                                            oc.wrBy = p.string("name"); // (joueur invité, sans compte)
                                    }
                            }
                        result.categories.push_back(std::move(oc));
                    }
            if (result.categories.empty() && result.error.empty())
                result.error = "Aucune catégorie officielle pour ce jeu";
        }
    }
    SDL_Log("speedrun.com : %s, %zu catégorie(s)%s", result.gameName.c_str(), result.categories.size(),
            result.error.empty() ? "" : (" (" + result.error + ")").c_str());
    std::lock_guard lock(m_mutex);
    m_result = std::move(result);
    m_busy = false;
}
