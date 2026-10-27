#include "Input.hpp"

#include "Utf8.hpp"

#include <libretro.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace {

// Au-delà de la moitié de sa course, un axe compte comme "appuyé".
constexpr int16_t kAxisThreshold = 16000;

Binding key(SDL_Scancode s) { return {Binding::Type::Key, s, 0}; }
Binding padButton(SDL_GamepadButton b) { return {Binding::Type::PadButton, b, 0}; }
Binding padAxis(SDL_GamepadAxis a, int dir) { return {Binding::Type::PadAxis, a, dir}; }
Binding joyButton(int b) { return {Binding::Type::JoyButton, b, 0}; }
Binding joyAxis(int a, int dir) { return {Binding::Type::JoyAxis, a, dir}; }
Binding joyHat(int h, int dir) { return {Binding::Type::JoyHat, h, dir}; }

const char* padButtonName(int b)
{
    switch (b) {
    case SDL_GAMEPAD_BUTTON_SOUTH: return "Bouton du bas";
    case SDL_GAMEPAD_BUTTON_EAST: return "Bouton de droite";
    case SDL_GAMEPAD_BUTTON_WEST: return "Bouton de gauche";
    case SDL_GAMEPAD_BUTTON_NORTH: return "Bouton du haut";
    case SDL_GAMEPAD_BUTTON_BACK: return "Select / Back";
    case SDL_GAMEPAD_BUTTON_GUIDE: return "Bouton central";
    case SDL_GAMEPAD_BUTTON_START: return "Start";
    case SDL_GAMEPAD_BUTTON_LEFT_STICK: return "Clic stick gauche";
    case SDL_GAMEPAD_BUTTON_RIGHT_STICK: return "Clic stick droit";
    case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: return "Bouton L1 / LB";
    case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: return "Bouton R1 / RB";
    case SDL_GAMEPAD_BUTTON_DPAD_UP: return "Croix haut";
    case SDL_GAMEPAD_BUTTON_DPAD_DOWN: return "Croix bas";
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT: return "Croix gauche";
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: return "Croix droite";
    default: return "Bouton spécial";
    }
}

std::string padAxisName(int axis, int dir)
{
    switch (axis) {
    case SDL_GAMEPAD_AXIS_LEFTX: return dir > 0 ? "Stick gauche, droite" : "Stick gauche, gauche";
    case SDL_GAMEPAD_AXIS_LEFTY: return dir > 0 ? "Stick gauche, bas" : "Stick gauche, haut";
    case SDL_GAMEPAD_AXIS_RIGHTX: return dir > 0 ? "Stick droit, droite" : "Stick droit, gauche";
    case SDL_GAMEPAD_AXIS_RIGHTY: return dir > 0 ? "Stick droit, bas" : "Stick droit, haut";
    case SDL_GAMEPAD_AXIS_LEFT_TRIGGER: return "Gâchette L2 / LT";
    case SDL_GAMEPAD_AXIS_RIGHT_TRIGGER: return "Gâchette R2 / RT";
    default: return "Axe";
    }
}

std::string keyName(int scancode)
{
    // SDL donne les noms en anglais : on traduit les plus courants.
    struct Pair {
        const char* en;
        const char* fr;
    };
    static const Pair names[] = {{"Up", "Flèche haut"},     {"Down", "Flèche bas"},       {"Left", "Flèche gauche"},
                                 {"Right", "Flèche droite"}, {"Return", "Entrée"},          {"Space", "Espace"},
                                 {"Left Shift", "Maj gauche"}, {"Right Shift", "Maj droite"}, {"Left Ctrl", "Ctrl gauche"},
                                 {"Right Ctrl", "Ctrl droite"}, {"Backspace", "Retour arrière"}, {"Tab", "Tab"},
                                 {"Left Alt", "Alt"},       {"Right Alt", "Alt Gr"}};
    const SDL_Keycode code = SDL_GetKeyFromScancode(static_cast<SDL_Scancode>(scancode), SDL_KMOD_NONE, false);
    const std::string name = SDL_GetKeyName(code);
    for (const Pair& p : names)
        if (name == p.en)
            return p.fr;
    return name.empty() ? "Touche " + std::to_string(scancode) : "Touche " + name;
}

std::string hatName(int hat, int dir)
{
    const char* d = dir == SDL_HAT_UP ? "haut" : dir == SDL_HAT_DOWN ? "bas" : dir == SDL_HAT_LEFT ? "gauche"
                  : dir == SDL_HAT_RIGHT ? "droite" : "diagonale";
    return "Croix " + std::to_string(hat + 1) + ", " + d;
}

} // namespace

Input::~Input()
{
    // [C++] Boucle "range-based for" : parcourt tous les éléments du vector.
    for (Device& d : m_devices) {
        if (d.pad)
            SDL_CloseGamepad(d.pad);
        else if (d.joy)
            SDL_CloseJoystick(d.joy);
    }
}

// --- Liaisons par défaut ---------------------------------------------------

Input::Bindings Input::defaultKeyboard()
{
    // Des "scancodes" : la POSITION physique de la touche. Les lettres
    // choisies sont au même endroit en AZERTY et en QWERTY.
    Bindings b{};
    b[RETRO_DEVICE_ID_JOYPAD_UP] = key(SDL_SCANCODE_UP);
    b[RETRO_DEVICE_ID_JOYPAD_DOWN] = key(SDL_SCANCODE_DOWN);
    b[RETRO_DEVICE_ID_JOYPAD_LEFT] = key(SDL_SCANCODE_LEFT);
    b[RETRO_DEVICE_ID_JOYPAD_RIGHT] = key(SDL_SCANCODE_RIGHT);
    b[RETRO_DEVICE_ID_JOYPAD_B] = key(SDL_SCANCODE_X);
    b[RETRO_DEVICE_ID_JOYPAD_A] = key(SDL_SCANCODE_C);
    b[RETRO_DEVICE_ID_JOYPAD_Y] = key(SDL_SCANCODE_S);
    b[RETRO_DEVICE_ID_JOYPAD_X] = key(SDL_SCANCODE_D);
    b[RETRO_DEVICE_ID_JOYPAD_L] = key(SDL_SCANCODE_E);
    b[RETRO_DEVICE_ID_JOYPAD_R] = key(SDL_SCANCODE_R);
    b[RETRO_DEVICE_ID_JOYPAD_START] = key(SDL_SCANCODE_RETURN);
    b[RETRO_DEVICE_ID_JOYPAD_SELECT] = key(SDL_SCANCODE_RSHIFT);
    return b;
}

Input::Bindings Input::defaultPad()
{
    // SDL et libretro nomment tous deux les boutons par position (bas,
    // droite, gauche, haut) : ça marche pour Xbox comme PlayStation.
    Bindings b{};
    b[RETRO_DEVICE_ID_JOYPAD_UP] = padButton(SDL_GAMEPAD_BUTTON_DPAD_UP);
    b[RETRO_DEVICE_ID_JOYPAD_DOWN] = padButton(SDL_GAMEPAD_BUTTON_DPAD_DOWN);
    b[RETRO_DEVICE_ID_JOYPAD_LEFT] = padButton(SDL_GAMEPAD_BUTTON_DPAD_LEFT);
    b[RETRO_DEVICE_ID_JOYPAD_RIGHT] = padButton(SDL_GAMEPAD_BUTTON_DPAD_RIGHT);
    b[RETRO_DEVICE_ID_JOYPAD_B] = padButton(SDL_GAMEPAD_BUTTON_SOUTH);
    b[RETRO_DEVICE_ID_JOYPAD_A] = padButton(SDL_GAMEPAD_BUTTON_EAST);
    b[RETRO_DEVICE_ID_JOYPAD_Y] = padButton(SDL_GAMEPAD_BUTTON_WEST);
    b[RETRO_DEVICE_ID_JOYPAD_X] = padButton(SDL_GAMEPAD_BUTTON_NORTH);
    b[RETRO_DEVICE_ID_JOYPAD_L] = padButton(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER);
    b[RETRO_DEVICE_ID_JOYPAD_R] = padButton(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER);
    b[RETRO_DEVICE_ID_JOYPAD_L2] = padAxis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER, 1);
    b[RETRO_DEVICE_ID_JOYPAD_R2] = padAxis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 1);
    b[RETRO_DEVICE_ID_JOYPAD_L3] = padButton(SDL_GAMEPAD_BUTTON_LEFT_STICK);
    b[RETRO_DEVICE_ID_JOYPAD_R3] = padButton(SDL_GAMEPAD_BUTTON_RIGHT_STICK);
    b[RETRO_DEVICE_ID_JOYPAD_START] = padButton(SDL_GAMEPAD_BUTTON_START);
    b[RETRO_DEVICE_ID_JOYPAD_SELECT] = padButton(SDL_GAMEPAD_BUTTON_BACK);
    b[kFirstAnalog + 0] = padAxis(SDL_GAMEPAD_AXIS_LEFTX, 1);
    b[kFirstAnalog + 1] = padAxis(SDL_GAMEPAD_AXIS_LEFTY, 1);
    b[kFirstAnalog + 2] = padAxis(SDL_GAMEPAD_AXIS_RIGHTX, 1);
    b[kFirstAnalog + 3] = padAxis(SDL_GAMEPAD_AXIS_RIGHTY, 1);
    return b;
}

Input::Bindings Input::defaultJoystick()
{
    // Manette inconnue : on devine une disposition courante. L'utilisateur
    // corrige ensuite dans Paramètres > Manettes.
    Bindings b{};
    b[RETRO_DEVICE_ID_JOYPAD_UP] = joyHat(0, SDL_HAT_UP);
    b[RETRO_DEVICE_ID_JOYPAD_DOWN] = joyHat(0, SDL_HAT_DOWN);
    b[RETRO_DEVICE_ID_JOYPAD_LEFT] = joyHat(0, SDL_HAT_LEFT);
    b[RETRO_DEVICE_ID_JOYPAD_RIGHT] = joyHat(0, SDL_HAT_RIGHT);
    const int order[] = {RETRO_DEVICE_ID_JOYPAD_B,  RETRO_DEVICE_ID_JOYPAD_A,      RETRO_DEVICE_ID_JOYPAD_Y,
                         RETRO_DEVICE_ID_JOYPAD_X,  RETRO_DEVICE_ID_JOYPAD_L,      RETRO_DEVICE_ID_JOYPAD_R,
                         RETRO_DEVICE_ID_JOYPAD_L2, RETRO_DEVICE_ID_JOYPAD_R2,     RETRO_DEVICE_ID_JOYPAD_SELECT,
                         RETRO_DEVICE_ID_JOYPAD_START, RETRO_DEVICE_ID_JOYPAD_L3, RETRO_DEVICE_ID_JOYPAD_R3};
    for (int i = 0; i < static_cast<int>(std::size(order)); ++i)
        b[order[i]] = joyButton(i);
    for (int i = 0; i < 4; ++i)
        b[kFirstAnalog + i] = joyAxis(i, 1);
    return b;
}

// --- Fichier input.ini --------------------------------------------------------
// Une ligne par liaison :  key <n°> <type> <index> <dir>
//                          pad <guid> <n°> <type> <index> <dir>

void Input::load(const std::filesystem::path& file)
{
    m_file = file;
    std::ifstream in(file);
    std::string line;
    bool keyboardSeen = false;
    while (std::getline(in, line)) {
        std::istringstream s(line);
        std::string kind, guid;
        int binding = 0, type = 0, index = 0, dir = 0;
        s >> kind;
        if (kind == "stickdpad") {
            if (s >> guid)
                m_stickDpad.insert(guid);
            continue;
        }
        if (kind == "pad")
            s >> guid;
        if (!(s >> binding >> type >> index >> dir) || binding < 0 || binding >= kBindingCount)
            continue;
        const Binding b{static_cast<Binding::Type>(type), index, dir};
        if (kind == "key") {
            if (!keyboardSeen)
                m_keyboard = {}; // le fichier décrit tout le clavier
            keyboardSeen = true;
            m_keyboard[binding] = b;
        } else if (kind == "pad") {
            m_padBindings[guid][binding] = b; // un tableau neuf commence vide
        }
    }
}

void Input::save() const
{
    if (m_file.empty())
        return;
    std::ofstream out(m_file, std::ios::trunc);
    auto write = [&](const std::string& prefix, const Bindings& bindings) {
        for (int i = 0; i < kBindingCount; ++i) {
            const Binding& b = bindings[i];
            out << prefix << ' ' << i << ' ' << static_cast<int>(b.type) << ' ' << b.index << ' ' << b.dir << '\n';
        }
    };
    write("key", m_keyboard);
    for (const auto& [guid, bindings] : m_padBindings)
        write("pad " + guid, bindings);
    for (const std::string& guid : m_stickDpad)
        out << "stickdpad " << guid << '\n';
}

// --- Dispositions toutes prêtes ------------------------------------------------

void Input::applyLayout(int device, Layout layout)
{
    if (device < 0 || device >= static_cast<int>(m_devices.size()))
        return;
    const Device& d = m_devices[device];
    Bindings b = d.pad ? defaultPad() : defaultJoystick();
    if (layout == Layout::Arcade) {
        // Un stick arcade : 2 rangées de 4 boutons.
        //   haut : Y  X  L  L2     (poings faible, moyen, fort... dans Street Fighter)
        //   bas  : B  A  R  R2     (pieds)
        if (d.pad) {
            // Les sticks "Xbox" nomment leurs boutons X Y RB LB (haut) et A B RT LT (bas).
            b[RETRO_DEVICE_ID_JOYPAD_Y] = padButton(SDL_GAMEPAD_BUTTON_WEST);
            b[RETRO_DEVICE_ID_JOYPAD_X] = padButton(SDL_GAMEPAD_BUTTON_NORTH);
            b[RETRO_DEVICE_ID_JOYPAD_L] = padButton(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER);
            b[RETRO_DEVICE_ID_JOYPAD_L2] = padButton(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER);
            b[RETRO_DEVICE_ID_JOYPAD_B] = padButton(SDL_GAMEPAD_BUTTON_SOUTH);
            b[RETRO_DEVICE_ID_JOYPAD_A] = padButton(SDL_GAMEPAD_BUTTON_EAST);
            b[RETRO_DEVICE_ID_JOYPAD_R] = padAxis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 1);
            b[RETRO_DEVICE_ID_JOYPAD_R2] = padAxis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER, 1);
        } else {
            // Encodeur USB "générique" : boutons 1 à 4 en haut, 5 à 8 en bas,
            // 9 = Select (coin), 10 = Start. Le stick est souvent un axe.
            const int top[] = {RETRO_DEVICE_ID_JOYPAD_Y, RETRO_DEVICE_ID_JOYPAD_X, RETRO_DEVICE_ID_JOYPAD_L,
                               RETRO_DEVICE_ID_JOYPAD_L2};
            const int bottom[] = {RETRO_DEVICE_ID_JOYPAD_B, RETRO_DEVICE_ID_JOYPAD_A, RETRO_DEVICE_ID_JOYPAD_R,
                                  RETRO_DEVICE_ID_JOYPAD_R2};
            for (int i = 0; i < 4; ++i) {
                b[top[i]] = joyButton(i);
                b[bottom[i]] = joyButton(4 + i);
            }
            b[RETRO_DEVICE_ID_JOYPAD_SELECT] = joyButton(8);
            b[RETRO_DEVICE_ID_JOYPAD_START] = joyButton(9);
            m_stickDpad.insert(d.guid);
        }
    }
    m_padBindings[d.guid] = b;
    m_capture = {};
    save();
}

bool Input::stickAsDpad(int device) const
{
    return device >= 0 && device < static_cast<int>(m_devices.size()) && m_stickDpad.count(m_devices[device].guid);
}

void Input::setStickAsDpad(int device, bool on)
{
    if (device < 0 || device >= static_cast<int>(m_devices.size()))
        return;
    if (on)
        m_stickDpad.insert(m_devices[device].guid);
    else
        m_stickDpad.erase(m_devices[device].guid);
    save();
}

// --- Branchements -------------------------------------------------------------

void Input::openConnected()
{
    int count = 0;
    SDL_JoystickID* ids = SDL_GetJoysticks(&count);
    for (int i = 0; i < count; ++i)
        addDevice(ids[i]);
    SDL_free(ids);
}

void Input::addDevice(SDL_JoystickID id)
{
    // Toutes les manettes passent par ici, connues ou non.
    for (const Device& d : m_devices)
        if (d.id == id)
            return; // déjà ouverte
    Device d;
    d.id = id;
    if (SDL_IsGamepad(id)) {
        d.pad = SDL_OpenGamepad(id);
        d.joy = d.pad ? SDL_GetGamepadJoystick(d.pad) : nullptr;
    }
    if (!d.joy)
        d.joy = SDL_OpenJoystick(id);
    if (!d.joy)
        return;
    char guid[33];
    SDL_GUIDToString(SDL_GetJoystickGUID(d.joy), guid, sizeof(guid));
    d.guid = guid;
    const char* name = d.pad ? SDL_GetGamepadName(d.pad) : SDL_GetJoystickName(d.joy);
    d.name = name ? name : "Manette";
    m_devices.push_back(d);
    SDL_Log("Manette branchée (joueur %zu, %s) : %s", m_devices.size(), d.pad ? "reconnue" : "générique",
            d.name.c_str());
}

void Input::handleEvent(const SDL_Event& event)
{
    switch (event.type) {
    case SDL_EVENT_JOYSTICK_ADDED:
        addDevice(event.jdevice.which);
        return;
    case SDL_EVENT_JOYSTICK_REMOVED:
        for (auto it = m_devices.begin(); it != m_devices.end(); ++it) {
            if (it->id == event.jdevice.which) {
                SDL_Log("Manette débranchée : %s", it->name.c_str());
                if (it->pad)
                    SDL_CloseGamepad(it->pad);
                else
                    SDL_CloseJoystick(it->joy);
                m_devices.erase(it);
                break;
            }
        }
        m_capture = {};
        return;
    default:
        break;
    }

    // --- Capture d'une nouvelle liaison ("appuyez sur une touche...").
    if (!m_capture.active)
        return;
    const bool analog = m_capture.binding >= kFirstAnalog;
    if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
        if (event.key.scancode == SDL_SCANCODE_ESCAPE)
            m_capture = {};
        else if (m_capture.device == -1 && !analog)
            assign(key(event.key.scancode));
        return;
    }
    if (m_capture.device < 0 || m_capture.device >= static_cast<int>(m_devices.size()))
        return;
    const Device& dev = m_devices[m_capture.device];
    if (dev.pad) {
        if (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN && event.gbutton.which == dev.id && !analog)
            assign(padButton(static_cast<SDL_GamepadButton>(event.gbutton.button)));
        else if (event.type == SDL_EVENT_GAMEPAD_AXIS_MOTION && event.gaxis.which == dev.id &&
                 std::abs(event.gaxis.value) > kAxisThreshold * 1.5)
            assign(padAxis(static_cast<SDL_GamepadAxis>(event.gaxis.axis), event.gaxis.value > 0 ? 1 : -1));
    } else {
        if (event.type == SDL_EVENT_JOYSTICK_BUTTON_DOWN && event.jbutton.which == dev.id && !analog)
            assign(joyButton(event.jbutton.button));
        else if (event.type == SDL_EVENT_JOYSTICK_AXIS_MOTION && event.jaxis.which == dev.id &&
                 std::abs(event.jaxis.value) > kAxisThreshold * 1.5)
            assign(joyAxis(event.jaxis.axis, event.jaxis.value > 0 ? 1 : -1));
        else if (event.type == SDL_EVENT_JOYSTICK_HAT_MOTION && event.jhat.which == dev.id &&
                 event.jhat.value != SDL_HAT_CENTERED && !analog)
            assign(joyHat(event.jhat.hat, event.jhat.value));
    }
}

void Input::assign(const Binding& binding)
{
    Binding b = binding;
    // Pour un stick, on garde le sens naturel (droite / bas = positif).
    if (m_capture.binding >= kFirstAnalog)
        b.dir = 1;
    if (m_capture.device == -1)
        m_keyboard[m_capture.binding] = b;
    else
        bindingsFor(m_devices[m_capture.device])[m_capture.binding] = b;
    m_capture = {};
    m_lastCapture = SDL_GetTicks();
    save();
}

Input::Bindings& Input::bindingsFor(const Device& device)
{
    auto it = m_padBindings.find(device.guid);
    if (it == m_padBindings.end())
        it = m_padBindings.emplace(device.guid, device.pad ? defaultPad() : defaultJoystick()).first;
    return it->second;
}

const Input::Bindings* Input::findBindings(const Device& device) const
{
    const auto it = m_padBindings.find(device.guid);
    return it == m_padBindings.end() ? nullptr : &it->second;
}

// --- Lecture des manettes --------------------------------------------------------

bool Input::pressed(const Device* d, const Binding& b) const
{
    switch (b.type) {
    case Binding::Type::None: return false;
    case Binding::Type::Key: return !d && SDL_GetKeyboardState(nullptr)[b.index];
    case Binding::Type::PadButton:
        return d && d->pad && SDL_GetGamepadButton(d->pad, static_cast<SDL_GamepadButton>(b.index));
    case Binding::Type::PadAxis:
        return d && d->pad && SDL_GetGamepadAxis(d->pad, static_cast<SDL_GamepadAxis>(b.index)) * b.dir > kAxisThreshold;
    case Binding::Type::JoyButton: return d && SDL_GetJoystickButton(d->joy, b.index);
    case Binding::Type::JoyAxis: return d && SDL_GetJoystickAxis(d->joy, b.index) * b.dir > kAxisThreshold;
    case Binding::Type::JoyHat: return d && (SDL_GetJoystickHat(d->joy, b.index) & b.dir) == b.dir;
    }
    return false;
}

int16_t Input::axisValue(const Device& d, const Binding& b) const
{
    int value = 0;
    if (b.type == Binding::Type::PadAxis && d.pad)
        value = SDL_GetGamepadAxis(d.pad, static_cast<SDL_GamepadAxis>(b.index));
    else if (b.type == Binding::Type::JoyAxis)
        value = SDL_GetJoystickAxis(d.joy, b.index);
    value *= (b.dir < 0 ? -1 : 1);
    return static_cast<int16_t>(std::clamp(value, -32767, 32767));
}

Input::PadState Input::readDevice(const Device* d, bool keyboard) const
{
    PadState port;
    if (keyboard)
        for (int i = 0; i < kFirstAnalog; ++i)
            if (pressed(nullptr, m_keyboard[i]))
                port.buttons |= 1u << i;
    if (!d)
        return remapped(port);
    const Bindings* found = findBindings(*d);
    const Bindings defaults = found ? Bindings{} : (d->pad ? defaultPad() : defaultJoystick());
    const Bindings& b = found ? *found : defaults;
    for (int i = 0; i < kFirstAnalog; ++i)
        if (pressed(d, b[i]))
            port.buttons |= 1u << i;
    // Stick qui pilote aussi la croix (sticks arcade en mode "analogique",
    // encodeurs USB) : les jeux rétro ne lisent que la croix.
    if (m_stickDpad.count(d->guid)) {
        const int x = d->pad ? SDL_GetGamepadAxis(d->pad, SDL_GAMEPAD_AXIS_LEFTX) : SDL_GetJoystickAxis(d->joy, 0);
        const int y = d->pad ? SDL_GetGamepadAxis(d->pad, SDL_GAMEPAD_AXIS_LEFTY) : SDL_GetJoystickAxis(d->joy, 1);
        constexpr int kThreshold = 16000; // la moitié de la course
        if (x < -kThreshold) port.buttons |= 1u << RETRO_DEVICE_ID_JOYPAD_LEFT;
        if (x > kThreshold) port.buttons |= 1u << RETRO_DEVICE_ID_JOYPAD_RIGHT;
        if (y < -kThreshold) port.buttons |= 1u << RETRO_DEVICE_ID_JOYPAD_UP;
        if (y > kThreshold) port.buttons |= 1u << RETRO_DEVICE_ID_JOYPAD_DOWN;
    }
    port.analog[RETRO_DEVICE_INDEX_ANALOG_LEFT][RETRO_DEVICE_ID_ANALOG_X] = axisValue(*d, b[kFirstAnalog + 0]);
    port.analog[RETRO_DEVICE_INDEX_ANALOG_LEFT][RETRO_DEVICE_ID_ANALOG_Y] = axisValue(*d, b[kFirstAnalog + 1]);
    port.analog[RETRO_DEVICE_INDEX_ANALOG_RIGHT][RETRO_DEVICE_ID_ANALOG_X] = axisValue(*d, b[kFirstAnalog + 2]);
    port.analog[RETRO_DEVICE_INDEX_ANALOG_RIGHT][RETRO_DEVICE_ID_ANALOG_Y] = axisValue(*d, b[kFirstAnalog + 3]);
    return remapped(port);
}

bool Input::padChordHeld(SDL_GamepadButton a, SDL_GamepadButton b) const
{
    for (const Device& d : m_devices)
        if (d.pad && SDL_GetGamepadButton(d.pad, a) && SDL_GetGamepadButton(d.pad, b))
            return true;
    return false;
}

void Input::setRemap(const std::array<int, 18>& table)
{
    m_remap = table;
}

Input::PadState Input::remapped(PadState in) const
{
    // Commandes propres à la console (Remaps) : le bouton n°c vu par le core
    // vient du bouton m_remap[c] de la manette (-1 : désactivé). Appliqué en
    // LISANT la manette : en ligne, ce sont les touches déjà réassignées qui
    // partent sur le réseau, chacun peut donc avoir ses propres réglages.
    uint16_t out = 0;
    for (int c = 0; c < 16; ++c)
        if (m_remap[c] >= 0 && ((in.buttons >> m_remap[c]) & 1u))
            out |= static_cast<uint16_t>(1u << c);
    in.buttons = out;
    // Les sticks : chaque stick du core vient d'un stick de la manette
    // (0 = gauche, 1 = droit, +2 = vertical inversé, -1 = aucun).
    const PadState source = in;
    for (int c = 0; c < 2; ++c) {
        const int s = m_remap[16 + c];
        if (s < 0) {
            in.analog[c][0] = in.analog[c][1] = 0;
            continue;
        }
        in.analog[c][0] = source.analog[s & 1][0];
        const int y = source.analog[s & 1][1];
        in.analog[c][1] = static_cast<int16_t>(s >= 2 ? std::clamp(-y, -32767, 32767) : y);
    }
    return in;
}

void Input::setGameRect(float x, float y, float w, float h)
{
    m_gameRect[0] = x;
    m_gameRect[1] = y;
    m_gameRect[2] = w;
    m_gameRect[3] = h;
}

void Input::poll()
{
    // La souris : déplacement depuis la dernière image, et position sur
    // l'image du jeu (pour viser avec la Wiimote).
    m_mouseButtons = SDL_GetRelativeMouseState(&m_mouseDx, &m_mouseDy);
    float mx = 0.0f, my = 0.0f;
    SDL_GetMouseState(&mx, &my);
    if (m_gameRect[2] > 0.0f && m_gameRect[3] > 0.0f) {
        const float nx = (mx - m_gameRect[0]) / m_gameRect[2] * 2.0f - 1.0f;
        const float ny = (my - m_gameRect[1]) / m_gameRect[3] * 2.0f - 1.0f;
        m_pointerInside = nx >= -1.0f && nx <= 1.0f && ny >= -1.0f && ny <= 1.0f;
        m_pointerX = std::clamp(nx, -1.0f, 1.0f);
        m_pointerY = std::clamp(ny, -1.0f, 1.0f);
    }
    if (m_override) {
        m_ports = m_overridePorts; // touches venues du réseau
        return;
    }
    m_ports.fill({});
    // Joueur 1 : le clavier et/ou une manette, selon la source choisie.
    m_ports[0] = sampleLocal();
    if (m_source != Source::Auto)
        return;
    if (m_keyboardSeparate)
        m_ports[0] = readDevice(nullptr, true); // le clavier seul
    // Les manettes suivantes : joueurs 2, 3, 4. Si le clavier est un joueur
    // à part, TOUTES les manettes sont décalées d'un cran (manette 1 = joueur 2).
    const size_t first = m_keyboardSeparate ? 0 : 1;
    for (size_t d = first; d < m_devices.size(); ++d) {
        const size_t port = m_keyboardSeparate ? d + 1 : d;
        if (port < kMaxPorts)
            m_ports[port] = readDevice(&m_devices[d], false);
    }
}

Input::PadState Input::sampleLocal() const
{
    PadState state;
    switch (m_source) {
    case Source::KeyboardOnly:
        state = readDevice(nullptr, true);
        break;
    case Source::Gamepad:
        state = m_sourcePad < static_cast<int>(m_devices.size()) ? readDevice(&m_devices[m_sourcePad], false) : PadState{};
        break;
    case Source::Auto:
        // (En ligne, c'est le seul joueur de ce PC : clavier ET 1re manette,
        // même si le clavier est "joueur à part" pour les parties locales.)
        state = readDevice(m_devices.empty() ? nullptr : &m_devices[0], true);
        break;
    }
    state.buttons |= m_touchButtons; // (la manette tactile, s'il y en a une)
    return state;
}

void Input::setOverride(const std::array<PadState, 4>& ports)
{
    m_override = true;
    for (size_t i = 0; i < kMaxPorts && i < ports.size(); ++i)
        m_overridePorts[i] = ports[i];
}

int16_t Input::state(unsigned port, unsigned device, unsigned index, unsigned id) const
{
    if (port >= kMaxPorts)
        return 0;
    const PortState& p = m_ports[port];

    // [C++] device peut être un "sous-type" (Dolphin : 769 = Wiimote +
    // Nunchuk, qui est une sorte de RETRO_DEVICE_JOYPAD) : les 8 bits du bas
    // donnent le type de base. Sans ce masque, aucun bouton ne passait.
    switch (device & RETRO_DEVICE_MASK) {
    case RETRO_DEVICE_MOUSE:
        // La souris (joueur 1) : déplacement depuis la dernière image, et boutons.
        if (port != 0 || m_override)
            return 0;
        switch (id) {
        case RETRO_DEVICE_ID_MOUSE_X: return static_cast<int16_t>(m_mouseDx);
        case RETRO_DEVICE_ID_MOUSE_Y: return static_cast<int16_t>(m_mouseDy);
        case RETRO_DEVICE_ID_MOUSE_LEFT: return (m_mouseButtons & SDL_BUTTON_LMASK) ? 1 : 0;
        case RETRO_DEVICE_ID_MOUSE_RIGHT: return (m_mouseButtons & SDL_BUTTON_RMASK) ? 1 : 0;
        case RETRO_DEVICE_ID_MOUSE_MIDDLE: return (m_mouseButtons & SDL_BUTTON_MMASK) ? 1 : 0;
        default: return 0;
        }
    case RETRO_DEVICE_POINTER:
        // Un "pointeur" : la position de la souris sur l'IMAGE du jeu, de
        // -32767 (bord gauche/haut) à 32767 (bord droit/bas). C'est ce qui
        // vise avec la Wiimote quand Dolphin est réglé sur la souris.
        if (port != 0 || index != 0 || m_override)
            return 0;
        switch (id) {
        case RETRO_DEVICE_ID_POINTER_X: return static_cast<int16_t>(m_pointerX * 0x7FFF);
        case RETRO_DEVICE_ID_POINTER_Y: return static_cast<int16_t>(m_pointerY * 0x7FFF);
        case RETRO_DEVICE_ID_POINTER_PRESSED: return (m_mouseButtons & SDL_BUTTON_LMASK) ? 1 : 0;
        case RETRO_DEVICE_ID_POINTER_IS_OFFSCREEN: return m_pointerInside ? 0 : 1;
        case RETRO_DEVICE_ID_POINTER_COUNT: return 1;
        default: return 0;
        }
    case RETRO_DEVICE_JOYPAD:
        // Le core peut demander tous les boutons d'un coup (plus rapide).
        if (id == RETRO_DEVICE_ID_JOYPAD_MASK)
            return static_cast<int16_t>(p.buttons);
        return id < 16 ? static_cast<int16_t>((p.buttons >> id) & 1u) : 0;
    case RETRO_DEVICE_ANALOG:
        if (index < 2 && id < 2)
            return p.analog[index][id];
        return 0;
    default:
        return 0;
    }
}

// --- Pour l'écran Paramètres -------------------------------------------------------

std::vector<Input::DeviceInfo> Input::devices() const
{
    std::vector<DeviceInfo> list;
    for (size_t i = 0; i < m_devices.size(); ++i)
        list.push_back({m_devices[i].name, m_devices[i].pad != nullptr, static_cast<int>(i) + (m_keyboardSeparate ? 2 : 1)});
    return list;
}

std::vector<std::string> Input::gamepadNames() const
{
    std::vector<std::string> names;
    for (const Device& d : m_devices)
        names.push_back(d.name);
    return names;
}

const char* Input::bindingName(int binding)
{
    static const char* const names[kBindingCount] = {
        "B (bouton du bas)", "Y (bouton de gauche)", "Select", "Start", "Croix haut", "Croix bas", "Croix gauche",
        "Croix droite", "A (bouton de droite)", "X (bouton du haut)", "L", "R", "L2", "R2", "L3 (clic stick gauche)",
        "R3 (clic stick droit)", "Stick gauche, horizontal", "Stick gauche, vertical", "Stick droit, horizontal",
        "Stick droit, vertical"};
    return binding >= 0 && binding < kBindingCount ? names[binding] : "";
}

std::string Input::bindingLabel(int device, int binding) const
{
    const Binding* b = nullptr;
    Bindings defaults{};
    if (device == -1) {
        b = &m_keyboard[binding];
    } else if (device < static_cast<int>(m_devices.size())) {
        const Device& d = m_devices[device];
        const Bindings* found = findBindings(d);
        defaults = d.pad ? defaultPad() : defaultJoystick();
        b = found ? &(*found)[binding] : &defaults[binding];
    }
    if (!b)
        return "—";
    const bool analog = binding >= kFirstAnalog;
    switch (b->type) {
    case Binding::Type::None: return "Aucune";
    case Binding::Type::Key: return keyName(b->index);
    case Binding::Type::PadButton: return padButtonName(b->index);
    case Binding::Type::PadAxis: {
        if (analog) {
            const char* sticks[] = {"Stick gauche, horizontal", "Stick gauche, vertical", "Stick droit, horizontal",
                                    "Stick droit, vertical", "Gâchette gauche", "Gâchette droite"};
            return b->index >= 0 && b->index < 6 ? sticks[b->index] : "Axe";
        }
        return padAxisName(b->index, b->dir);
    }
    case Binding::Type::JoyButton: return "Bouton " + std::to_string(b->index + 1);
    case Binding::Type::JoyAxis:
        return "Axe " + std::to_string(b->index + 1) + (analog ? "" : (b->dir > 0 ? " +" : " -"));
    case Binding::Type::JoyHat: return hatName(b->index, b->dir);
    }
    return "—";
}

void Input::startCapture(int device, int binding)
{
    m_capture = {true, device, binding};
}

bool Input::isCapturing(int device, int binding) const
{
    return m_capture.active && m_capture.device == device && m_capture.binding == binding;
}

void Input::resetBindings(int device)
{
    if (device == -1)
        m_keyboard = defaultKeyboard();
    else if (device < static_cast<int>(m_devices.size()))
        m_padBindings[m_devices[device].guid] = m_devices[device].pad ? defaultPad() : defaultJoystick();
    m_capture = {};
    save();
}
