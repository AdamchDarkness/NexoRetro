#pragma once
// Jni : appeler le code Java d'Android depuis le C++.
//
// Une appli Android est d'abord une appli Java : c'est Java qui sait
// télécharger en HTTPS, chiffrer, lire un .zip ou ouvrir les réglages du
// téléphone. Le C++ y accède par la "JNI" (Java Native Interface), à travers
// une seule classe Java faite pour ça : org.nexoretro.app.NexoBridge
// (android/app/src/main/java/org/nexoretro/app/NexoBridge.java).
//
// [JNI] Chaque thread qui appelle Java a besoin de son "JNIEnv" ; SDL nous le
// donne (et y rattache le thread au besoin). La classe NexoBridge doit, elle,
// être trouvée une fois depuis le thread principal (init), puis gardée.

#include <jni.h>

#include <filesystem>
#include <string>
#include <vector>

namespace android {

// À appeler au début de main(), sur le thread principal.
void init();

JNIEnv* env();
jclass bridge(); // org.nexoretro.app.NexoBridge (référence globale)

// Conversions entre chaînes et tableaux Java et C++.
jstring toJava(const std::string& text);
std::string fromJava(jstring text);
jbyteArray toJava(const void* data, size_t size);
std::string fromJava(jbyteArray bytes);

// Le stockage privé de l'appli (code et fichiers internes).
std::filesystem::path privateDir();
// Le dossier des données visibles par l'utilisateur : /sdcard/NexoRetro si
// l'appli a le droit d'y écrire, sinon son dossier dans Android/data.
std::filesystem::path dataDir();
// Version TV (télécommande, Big Picture) ou tablette/téléphone (écran tactile) ?
bool isTv();
// L'accès à tous les fichiers (pour lire les jeux rangés n'importe où).
bool hasAllFilesAccess();
void requestAllFilesAccess();
// Recopie les fichiers de l'APK (assets/) dans le stockage privé, une fois par version.
void prepareAssets(const char* version);

} // namespace android
