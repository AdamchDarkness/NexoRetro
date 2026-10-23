#pragma once
// ExternalProcess : lancer un émulateur externe (Ryujinx, RPCS3, xemu...)
// et savoir quand il se ferme (pour compter le temps de jeu).
//
// Ces émulateurs cherchent leur configuration à côté de leur programme : on
// les lance donc depuis leur propre dossier (ce que SDL ne permet pas encore,
// d'où l'appel direct à Windows).

#include <filesystem>
#include <string>
#include <vector>

class ExternalProcess {
public:
    ExternalProcess() = default;
    ~ExternalProcess(); // ne ferme PAS l'émulateur : il continue sa vie
    ExternalProcess(const ExternalProcess&) = delete;
    ExternalProcess& operator=(const ExternalProcess&) = delete;

    // args : les arguments en UTF-8 (sans le programme lui-même).
    bool start(const std::filesystem::path& exe, const std::vector<std::string>& args, std::string& error);
    // Toujours ouvert ? (Ne bloque jamais.)
    bool running();

private:
    void* m_process = nullptr; // HANDLE Windows
};
