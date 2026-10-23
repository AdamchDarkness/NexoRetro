#pragma once
// Input : clavier et manettes, traduits en "RetroPad".
//
// Le RetroPad est la manette virtuelle de libretro : une croix, A/B/X/Y,
// L/R, L2/R2, L3/R3, Start, Select et deux sticks. Chaque core traduit ensuite
// le RetroPad vers la vraie manette de sa console.
//
// Chaque bouton du RetroPad est relié à une "liaison" (Binding) : une touche
// du clavier, un bouton, un axe ou une croix de manette. Les liaisons se
// changent dans Paramètres > Manettes et sont enregistrées dans input.ini,
// par modèle de manette (deux manettes identiques partagent leurs réglages).
//
// Deux sortes de manettes :
//  - les manettes "connues" de SDL (Xbox, PlayStation, Switch...) : SDL sait
//    où sont leurs boutons, on parle en "bouton du bas", "gâchette gauche"...
//  - les manettes "génériques" (USB sans marque, sticks arcade...) : SDL ne
//    voit que des numéros ("bouton 3", "axe 1"). On les accepte aussi.

#include <SDL3/SDL.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <vector>

struct Binding {
    enum class Type : uint8_t {
        None,
        Key,       // index = scancode du clavier
        PadButton, // index = SDL_GamepadButton
        PadAxis,   // index = SDL_GamepadAxis, dir = +1 ou -1
        JoyButton, // index = numéro de bouton
        JoyAxis,   // index = numéro d'axe, dir = +1 ou -1
        JoyHat     // index = numéro de croix, dir = SDL_HAT_UP...
    };
    Type type = Type::None;
    int index = 0;
    int dir = 0;
};

class Input {
public:
    // 16 boutons du RetroPad + 4 axes analogiques (sticks gauche et droit).
    static constexpr int kBindingCount = 20;
    static constexpr int kFirstAnalog = 16;
    using Bindings = std::array<Binding, kBindingCount>;

    Input() = default;
    ~Input();
    Input(const Input&) = delete;
    Input& operator=(const Input&) = delete;

    void load(const std::filesystem::path& file);
    void save() const;

    // Ouvre les manettes déjà branchées au démarrage.
    void openConnected();

    // Ce que le joueur de CE PC utilise. Par défaut : le clavier ET la
    // première manette. Pour lancer deux émulateurs sur le même PC (tester
    // le jeu en ligne seul), l'un prend le clavier, l'autre une manette.
    enum class Source { Auto, KeyboardOnly, Gamepad };
    void setSource(Source source, int gamepadIndex = 0)
    {
        m_source = source;
        m_sourcePad = gamepadIndex;
    }
    // Le clavier, joueur à part ? false (défaut) : le clavier et la 1re
    // manette pilotent ensemble le joueur 1, la 2e manette le joueur 2...
    // true : le clavier est le joueur 1, les manettes les joueurs 2, 3, 4
    // (pour jouer à deux sur un même PC avec une seule manette).
    void setKeyboardSeparate(bool separate) { m_keyboardSeparate = separate; }
    bool keyboardSeparate() const { return m_keyboardSeparate; }
    // Branchement / débranchement, et capture d'une nouvelle liaison.
    void handleEvent(const SDL_Event& event);

    // Photographie l'état de toutes les touches (appelé une fois par image).
    void poll();
    // Où est l'image du jeu dans la fenêtre (en points SDL) : pour traduire la
    // position de la souris en "pointeur" (viser avec la Wiimote).
    void setGameRect(float x, float y, float w, float h);
    // Commandes propres à la console en cours (voir Remaps) : [bouton du
    // core] = bouton de la manette, -1 = désactivé. Identité hors du jeu.
    void setRemap(const std::array<int, 18>& table); // 16 boutons + 2 sticks (voir Remaps)
    // Raccourci manette : ces deux boutons tenus ensemble sur une manette
    // (Select + R = avance rapide...), quel que soit le joueur.
    bool padChordHeld(SDL_GamepadButton a, SDL_GamepadButton b) const;

    // --- Pour le jeu en ligne ------------------------------------------------
    struct PadState {
        uint16_t buttons = 0;      // 1 bit par bouton du RetroPad
        int16_t analog[2][2] = {}; // [stick gauche/droit][axe X/Y]
    };
    // Les touches du joueur de CE PC (clavier + première manette).
    PadState sampleLocal() const;
    // Pendant une partie en ligne, le core reçoit les touches de tous les
    // joueurs (venues du réseau) au lieu de lire les manettes branchées.
    void setOverride(const std::array<PadState, 4>& ports);
    void clearOverride() { m_override = false; }
    // Le core demande : "le bouton id de la manette port est-il appuyé ?"
    int16_t state(unsigned port, unsigned device, unsigned index, unsigned id) const;

    // --- Pour l'écran Paramètres -----------------------------------------
    struct DeviceInfo {
        std::string name;
        bool known = false; // manette reconnue par SDL
        int player = 0;     // 1, 2...
    };
    std::vector<DeviceInfo> devices() const;
    std::vector<std::string> gamepadNames() const;

    static const char* bindingName(int binding);  // "B (bouton du bas)"...
    // device = -1 pour le clavier, sinon l'indice dans devices().
    std::string bindingLabel(int device, int binding) const;
    void startCapture(int device, int binding);
    void cancelCapture() { m_capture = {}; }
    bool isCapturing(int device, int binding) const;
    // Date (SDL_GetTicks) de la dernière liaison enregistrée.
    Uint64 lastCaptureTime() const { return m_lastCapture; }
    void resetBindings(int device);
    // Dispositions toutes prêtes : manette classique, ou stick arcade
    // (2 rangées de 4 boutons : Y X L L2 en haut, B A R R2 en bas).
    enum class Layout { Pad, Arcade };
    void applyLayout(int device, Layout layout);
    // Le stick gauche pilote aussi la croix (sticks arcade, encodeurs USB).
    bool stickAsDpad(int device) const;
    void setStickAsDpad(int device, bool on);

private:
    struct Device {
        SDL_JoystickID id = 0;
        SDL_Gamepad* pad = nullptr;   // si la manette est connue
        SDL_Joystick* joy = nullptr;  // toujours présent
        std::string guid;             // identifiant du modèle
        std::string name;
    };
    using PortState = PadState;
    struct Capture {
        bool active = false;
        int device = -1;
        int binding = 0;
    };

    void addDevice(SDL_JoystickID id);
    static Bindings defaultKeyboard();
    static Bindings defaultPad();
    static Bindings defaultJoystick();
    Bindings& bindingsFor(const Device& device);
    const Bindings* findBindings(const Device& device) const;
    void assign(const Binding& binding);
    bool pressed(const Device* device, const Binding& b) const;
    int16_t axisValue(const Device& device, const Binding& b) const;
    // Lit une manette (et le clavier si keyboard est vrai).
    PadState readDevice(const Device* device, bool keyboard) const;
    PadState remapped(PadState in) const;
    std::array<int, 18> m_remap{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 0, 1};

    static constexpr unsigned kMaxPorts = 4;
    std::array<PortState, kMaxPorts> m_ports{};
    std::vector<Device> m_devices; // dans l'ordre de branchement = ordre des joueurs

    Bindings m_keyboard = defaultKeyboard();
    std::map<std::string, Bindings> m_padBindings; // par GUID
    std::filesystem::path m_file;
    Capture m_capture;
    Uint64 m_lastCapture = 0;
    bool m_override = false;
    // La souris (joueur 1).
    float m_mouseDx = 0.0f, m_mouseDy = 0.0f;
    uint32_t m_mouseButtons = 0;
    float m_pointerX = 0.0f, m_pointerY = 0.0f; // -1..1 sur l'image du jeu
    bool m_pointerInside = false;
    float m_gameRect[4] = {};
    std::array<PortState, kMaxPorts> m_overridePorts{};
    Source m_source = Source::Auto;
    int m_sourcePad = 0;
    bool m_keyboardSeparate = false;
    std::set<std::string> m_stickDpad; // GUID des manettes dont le stick pilote la croix
};
